#include "net/makapix.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "display.hpp"
#include "net/clock.hpp"
#include "sdcard.hpp"
#include "net/wifi.hpp"

namespace p64::makapix {
namespace {

constexpr const char *TAG = "makapix";
constexpr size_t kMaxJsonBytes = 64 * 1024;
constexpr size_t kMaxGifBytes = CONFIG_P64_MAKAPIX_MAX_BYTES;
constexpr int kMaxDim = CONFIG_P64_MAKAPIX_MAX_DIMENSION;  // width_max/height_max of the rotation query
constexpr size_t kRecentSize = 32;
constexpr int kAttemptsPerRequest = 2;
constexpr int kRedrawsOnRepeat = 3;
constexpr uint32_t kNetworkWaitMs = 90 * 1000;  // give up waiting for Wi-Fi/NTP after this
constexpr int kMaxRedirects = 3;
// On-demand (web) requests.
constexpr size_t kMaxPlayBytes = CONFIG_P64_WEB_MAX_BYTES;
constexpr int kMaxPlayDim = CONFIG_P64_WEB_MAX_DIMENSION;
constexpr uint32_t kPlayNetworkWaitMs = 10 * 1000;  // a web request fails fast when offline
constexpr int kPlayAttempts = 2;

std::mutex g_mutex;
std::optional<Artwork> g_ready;
bool g_in_flight = false;
std::atomic<bool> g_paused{false};
std::atomic<bool> g_fetching{false};
TaskHandle_t g_task = nullptr;
std::deque<std::string> g_recent;  // task-private
char g_user_agent[48] = "p64";
// On-demand requests, under g_mutex.
std::optional<PlayRequest> g_play_request;
uint32_t g_play_request_id = 0;
std::optional<Artwork> g_play_ready;
uint32_t g_play_ready_seconds = 0;
uint32_t g_play_ready_id = 0;
uint32_t g_play_cancelled_id = 0;  // results of requests up to this id are dropped
uint32_t g_next_play_id = 0;
PlayStatus g_play_status;

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

// GETs `url` into `out` (appending), capped at `max_bytes`. Returns the HTTP status,
// or a negative esp_err_t on transport failure.
int http_get(const char *url, const char *accept, std::vector<uint8_t> &out, size_t max_bytes) {
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 15000;
  cfg.user_agent = g_user_agent;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return -ESP_ERR_NO_MEM;
  if (accept) esp_http_client_set_header(client, "Accept", accept);

  ESP_LOGD(TAG, "GET %s", url);
  int64_t content_length = 0;
  int status = 0;
  for (int hop = 0;; ++hop) {
    const esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return -static_cast<int>(err);
    }
    content_length = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "status %d, %lld bytes", status, static_cast<long long>(content_length));
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (!redirect || hop >= kMaxRedirects) break;
    // Follow Location ourselves: esp_http_client only does so inside perform().
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    esp_http_client_close(client);
  }
  if (content_length > static_cast<int64_t>(max_bytes)) {
    ESP_LOGW(TAG, "response of %lld bytes exceeds the %u byte cap", static_cast<long long>(content_length),
             static_cast<unsigned>(max_bytes));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return -ESP_ERR_INVALID_SIZE;
  }
  if (content_length > 0) out.reserve(out.size() + static_cast<size_t>(content_length));

  uint8_t chunk[2048];
  while (true) {
    const int n = esp_http_client_read(client, reinterpret_cast<char *>(chunk), sizeof(chunk));
    if (n < 0) {
      ESP_LOGW(TAG, "read failed");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return -ESP_FAIL;
    }
    if (n == 0) break;
    if (out.size() + static_cast<size_t>(n) > max_bytes) {
      ESP_LOGW(TAG, "response exceeds the %u byte cap", static_cast<unsigned>(max_bytes));
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return -ESP_ERR_INVALID_SIZE;
    }
    out.insert(out.end(), chunk, chunk + n);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return status;
}

// ---------------------------------------------------------------------------
// Makapix API
// ---------------------------------------------------------------------------

struct Candidate {
  std::string sqid, title, artist;
  int width = 0, height = 0;
  size_t gif_bytes = 0;
};

const char *json_string(const cJSON *obj, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return (cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
}

int json_int(const cJSON *obj, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsNumber(v) ? v->valueint : 0;
}

// Asks the server for one random promoted GIF that fits the panel.
bool query_random(Candidate &c) {
  char url[256];
  std::snprintf(url, sizeof(url),
                "https://%s/api/post?promoted=true&sort=random&limit=1&width_max=%d&height_max=%d&file_format=gif",
                CONFIG_P64_MAKAPIX_HOST, kMaxDim, kMaxDim);
  std::vector<uint8_t> body;
  const int status = http_get(url, "application/json", body, kMaxJsonBytes);
  if (status != 200) {
    ESP_LOGW(TAG, "query failed (%d)", status);
    return false;
  }
  body.push_back(0);
  cJSON *root = cJSON_ParseWithLength(reinterpret_cast<const char *>(body.data()), body.size() - 1);
  if (!root) {
    ESP_LOGW(TAG, "query returned invalid JSON (%u bytes)", static_cast<unsigned>(body.size() - 1));
    return false;
  }
  bool ok = false;
  const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
  const cJSON *item = cJSON_IsArray(items) ? cJSON_GetArrayItem(items, 0) : nullptr;
  if (item) {
    c.sqid = json_string(item, "public_sqid");
    c.title = json_string(item, "title");
    c.width = json_int(item, "width");
    c.height = json_int(item, "height");
    const cJSON *owner = cJSON_GetObjectItemCaseSensitive(item, "owner");
    c.artist = owner ? json_string(owner, "handle") : "";
    c.gif_bytes = 0;
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(item, "files");
    const cJSON *file;
    cJSON_ArrayForEach(file, files) {
      if (std::strcmp(json_string(file, "format"), "gif") == 0) c.gif_bytes = static_cast<size_t>(json_int(file, "file_bytes"));
    }
    ok = !c.sqid.empty() && c.width > 0 && c.height > 0;
  } else {
    ESP_LOGW(TAG, "query returned no items");
  }
  cJSON_Delete(root);
  return ok;
}

bool download_gif(const std::string &sqid, std::vector<uint8_t> &gif) {
  char url[128];
  std::snprintf(url, sizeof(url), "https://%s/api/d/%s.gif", CONFIG_P64_MAKAPIX_HOST, sqid.c_str());
  gif.clear();
  const int status = http_get(url, "image/gif", gif, kMaxGifBytes);
  if (status != 200) {
    ESP_LOGW(TAG, "download of %s failed (%d)", sqid.c_str(), status);
    return false;
  }
  if (gif.size() < 6 || std::memcmp(gif.data(), "GIF8", 4) != 0) {
    ESP_LOGW(TAG, "download of %s is not a GIF", sqid.c_str());
    return false;
  }
  return true;
}

bool recently_shown(const std::string &sqid) {
  return std::find(g_recent.begin(), g_recent.end(), sqid) != g_recent.end();
}

void remember(const std::string &sqid) {
  g_recent.push_back(sqid);
  while (g_recent.size() > kRecentSize) g_recent.pop_front();
}

// One full attempt: pick a candidate the panel has not shown recently, download it.
bool fetch_one(Artwork &art) {
  Candidate c;
  bool found = false;
  for (int i = 0; i < kRedrawsOnRepeat && !found; ++i) {
    if (!query_random(c)) return false;
    if (recently_shown(c.sqid)) {
      ESP_LOGI(TAG, "%s shown recently, drawing again", c.sqid.c_str());
      continue;
    }
    found = true;
  }
  if (!found) {
    ESP_LOGI(TAG, "only recent picks came back, taking %s anyway", c.sqid.c_str());
  }
  if (c.width > kMaxDim || c.height > kMaxDim) {
    ESP_LOGW(TAG, "%s is %dx%d, larger than %d px; skipped", c.sqid.c_str(), c.width, c.height, kMaxDim);
    return false;
  }
  if (c.gif_bytes > kMaxGifBytes) {
    ESP_LOGW(TAG, "%s GIF is %u bytes, over the cap; skipped", c.sqid.c_str(), static_cast<unsigned>(c.gif_bytes));
    return false;
  }
  if (!download_gif(c.sqid, art.gif)) return false;
  art.sqid = c.sqid;
  art.title = c.title;
  art.artist = c.artist;
  art.width = c.width;
  art.height = c.height;
  remember(c.sqid);
  return true;
}

// Waits until Wi-Fi is up and, when `need_time`, the clock is set (TLS checks
// certificate dates). False when that takes longer than `max_ms`.
bool wait_for_network(bool need_time, uint32_t max_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(max_ms);
  while (!(wifi::connected() && (!need_time || clock::synced()))) {
    if (xTaskGetTickCount() >= deadline) return false;
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  return true;
}

// ---------------------------------------------------------------------------
// On-demand requests
// ---------------------------------------------------------------------------

// Reads the logical screen size from a GIF header.
bool gif_size(const std::vector<uint8_t> &gif, int &w, int &h) {
  if (gif.size() < 10 || std::memcmp(gif.data(), "GIF8", 4) != 0) return false;
  w = gif[6] | (gif[7] << 8);
  h = gif[8] | (gif[9] << 8);
  return w > 0 && h > 0;
}

// Header and size checks shared by downloads and card files.
bool validate_gif(Artwork &art, std::string &error) {
  if (!gif_size(art.gif, art.width, art.height)) {
    error = "not a GIF file";
    return false;
  }
  if (art.width > kMaxPlayDim || art.height > kMaxPlayDim) {
    error = std::to_string(art.width) + "x" + std::to_string(art.height) + " is larger than " +
            std::to_string(kMaxPlayDim) + " px";
    return false;
  }
  return true;
}

// Downloads a request into `art`; `error` explains a failure.
bool fetch_play(const PlayRequest &req, Artwork &art, std::string &error) {
  std::string url = req.url;
  if (!req.sqid.empty()) url = std::string("https://") + CONFIG_P64_MAKAPIX_HOST + "/api/d/" + req.sqid + ".gif";
  art = Artwork{};
  art.sqid = req.sqid;
  art.url = req.url;
  const int status = http_get(url.c_str(), "image/gif", art.gif, kMaxPlayBytes);
  if (status == -ESP_ERR_INVALID_SIZE) {
    error = "file larger than " + std::to_string(kMaxPlayBytes) + " bytes";
  } else if (status < 0) {
    error = std::string("download failed: ") + esp_err_to_name(-status);
  } else if (status == 404) {
    error = req.sqid.empty() ? "not found (404)" : "no such post, or it has no GIF (404)";
  } else if (status != 200) {
    error = "HTTP " + std::to_string(status);
  } else {
    return validate_gif(art, error);
  }
  return false;
}

// Reads a card file into `art`.
bool load_card_file(const PlayRequest &req, Artwork &art, std::string &error) {
  art = Artwork{};
  art.file = req.file;
  if (!sdcard::read_file(req.file, art.gif, kMaxPlayBytes, error)) return false;
  return validate_gif(art, error);
}

// Serves one request end to end: download, then hand the result (or the error) to the
// scene and the status page. Dropped when /stop or a newer request came first.
void serve_play(const PlayRequest &req, uint32_t id) {
  const bool from_card = !req.file.empty();
  const bool https = !req.sqid.empty() || req.url.rfind("https://", 0) == 0;
  const char *target = from_card ? req.file.c_str() : (req.sqid.empty() ? req.url.c_str() : req.sqid.c_str());
  Artwork art;
  std::string error;
  bool ok = false;
  if (from_card) {
    const TickType_t t0 = xTaskGetTickCount();
    ok = load_card_file(req, art, error);
    if (ok) {
      ESP_LOGI(TAG, "request #%lu: read %s from the card (%dx%d, %u bytes) in %lu ms", static_cast<unsigned long>(id),
               target, art.width, art.height, static_cast<unsigned>(art.gif.size()),
               static_cast<unsigned long>((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
    }
  } else if (!wait_for_network(https, kPlayNetworkWaitMs)) {
    error = wifi::connected() ? "clock not synced yet (TLS needs it)" : "Wi-Fi not connected";
  } else {
    for (int attempt = 1; attempt <= kPlayAttempts && !ok; ++attempt) {
      const TickType_t t0 = xTaskGetTickCount();
      ok = fetch_play(req, art, error);
      if (ok) {
        ESP_LOGI(TAG, "request #%lu: fetched %s (%dx%d, %u bytes) in %lu ms", static_cast<unsigned long>(id), target,
                 art.width, art.height, static_cast<unsigned>(art.gif.size()),
                 static_cast<unsigned long>((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
      } else if (attempt < kPlayAttempts && error.rfind("download failed", 0) == 0) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // transport trouble: one more try
      } else {
        break;
      }
    }
  }
  if (!ok) ESP_LOGW(TAG, "request #%lu (%s) failed: %s", static_cast<unsigned long>(id), target, error.c_str());
  if (ok && !art.sqid.empty()) remember(art.sqid);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (id <= g_play_cancelled_id) return;                 // /stop came first
  if (g_play_request && g_play_request_id > id) return;  // superseded meanwhile
  if (g_play_status.id == id) {
    g_play_status.state = ok ? PlayState::ready : PlayState::failed;
    g_play_status.error = ok ? "" : error;
  }
  if (ok) {
    g_play_ready = std::move(art);
    g_play_ready_seconds = req.seconds;
    g_play_ready_id = id;
  }
}

void fetcher_task(void *) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (g_paused.load()) vTaskDelay(pdMS_TO_TICKS(250));

    // Web requests first: someone is waiting for them.
    while (true) {
      std::optional<PlayRequest> req;
      uint32_t id = 0;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_play_request) break;
        req = std::move(*g_play_request);
        g_play_request.reset();
        id = g_play_request_id;
        g_play_status.state = PlayState::downloading;
      }
      g_fetching = true;
      serve_play(*req, id);
      g_fetching = false;
    }

    // Then the show's rotation.
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_in_flight || g_ready) {
        g_in_flight = false;
        continue;  // no rotation request, or still holding one nobody took
      }
    }
    Artwork art;
    bool ok = false;
    g_fetching = true;
    if (!wait_for_network(true, kNetworkWaitMs)) {
      ESP_LOGW(TAG, "network or time not ready after %lu s (wifi %d, ntp %d), trying anyway",
               static_cast<unsigned long>(kNetworkWaitMs / 1000), wifi::connected() ? 1 : 0, clock::synced() ? 1 : 0);
    }
    if (wifi::connected()) {
      for (int attempt = 1; attempt <= kAttemptsPerRequest && !ok; ++attempt) {
        const int64_t t0 = xTaskGetTickCount();
        ok = fetch_one(art);
        if (ok) {
          ESP_LOGI(TAG, "fetched %s \"%s\" by %s (%dx%d, %u bytes) in %lu ms", art.sqid.c_str(), art.title.c_str(),
                   art.artist.c_str(), art.width, art.height, static_cast<unsigned>(art.gif.size()),
                   static_cast<unsigned long>((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
        } else if (attempt < kAttemptsPerRequest) {
          vTaskDelay(pdMS_TO_TICKS(3000));
        }
      }
    }
    g_fetching = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ok) g_ready = std::move(art);
    g_in_flight = false;
  }
}

}  // namespace

void start() {
#if !defined(CONFIG_P64_MAKAPIX_ENABLE)
  ESP_LOGI(TAG, "disabled in menuconfig");
  return;
#endif
  if (g_task) return;
  std::snprintf(g_user_agent, sizeof(g_user_agent), "p64/%s", esp_app_get_description()->version);
  // TLS runs on this task's stack; the fetcher lives on the Wi-Fi core.
  const BaseType_t rc = xTaskCreatePinnedToCore(fetcher_task, "makapix", 24 * 1024, nullptr, 4, &g_task, 0);
  if (rc != pdPASS) {
    ESP_LOGE(TAG, "cannot create the fetcher task");
    g_task = nullptr;
    return;
  }
  ESP_LOGI(TAG, "fetcher ready: https://%s, GIFs up to %dx%d and %u bytes, user agent \"%s\"",
           CONFIG_P64_MAKAPIX_HOST, kMaxDim, kMaxDim, static_cast<unsigned>(kMaxGifBytes), g_user_agent);
}

void request_next() {
  if (!g_task) return;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_in_flight || g_ready) return;
    g_in_flight = true;
  }
  xTaskNotifyGive(g_task);
}

bool take_ready(Artwork &out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready) return false;
  out = std::move(*g_ready);
  g_ready.reset();
  return true;
}

void set_paused(bool paused) { g_paused = paused; }

bool busy() { return g_fetching.load(); }

uint32_t request_play(const PlayRequest &req) {
  if (!g_task) return 0;
  uint32_t id;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    id = ++g_next_play_id;
    g_play_request = req;
    g_play_request_id = id;
    g_play_ready.reset();  // a newer request supersedes a result nobody took yet
    g_play_status = PlayStatus{};
    g_play_status.id = id;
    g_play_status.state = PlayState::queued;
    g_play_status.target = !req.file.empty() ? req.file : (req.sqid.empty() ? req.url : req.sqid);
  }
  xTaskNotifyGive(g_task);
  return id;
}

bool take_play(Artwork &out, uint32_t &seconds, uint32_t &id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_play_ready) return false;
  out = std::move(*g_play_ready);
  g_play_ready.reset();
  seconds = g_play_ready_seconds;
  id = g_play_ready_id;
  return true;
}

void cancel_play() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_play_cancelled_id = g_next_play_id;
  g_play_request.reset();
  g_play_ready.reset();
  if (g_play_status.state == PlayState::queued || g_play_status.state == PlayState::downloading) {
    g_play_status.state = PlayState::idle;
    g_play_status.error = "cancelled by /stop";
  }
}

void set_play_error(uint32_t id, const char *error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_play_status.id != id) return;
  g_play_status.state = PlayState::failed;
  g_play_status.error = error;
}

PlayStatus play_status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_play_status;
}

}  // namespace p64::makapix

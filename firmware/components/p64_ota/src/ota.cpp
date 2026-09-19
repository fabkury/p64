#include "p64/ota/ota.hpp"

#include <cinttypes>
#include <cstring>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "p64/net/clock.hpp"
#include "p64/net/fetch.hpp"
#include "p64/net/wifi.hpp"
#include "p64/system/settings.hpp"
#include "sdkconfig.h"
#include "version.hpp"

namespace p64::ota {
namespace {

constexpr const char *TAG = "ota";
constexpr uint64_t kCheckPeriodUs = 12ULL * 3600 * 1000000;
constexpr uint64_t kFirstCheckUs = 90ULL * 1000000;
constexpr size_t kNotesMax = 600;

enum class Job : uint8_t { None, Check, Install };

std::mutex g_mutex;
Status g_status;
bool g_busy = false;
Job g_job = Job::None;
std::string g_job_url, g_job_sha;
esp_timer_handle_t g_timer = nullptr;
bool g_first_check_done = false;

void set_state(State s, const std::string &error = "") {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.state = s;
  g_status.error = error;
}

bool hex_to_bin(const std::string &hex, uint8_t out[32]) {
  if (hex.size() < 64) return false;
  for (int i = 0; i < 32; ++i) {
    unsigned v = 0;
    for (int k = 0; k < 2; ++k) {
      const char c = hex[i * 2 + k];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return false;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

// Reads the other slot's description for the rollback fields.
void refresh_rollback() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = running ? esp_ota_get_next_update_partition(running) : nullptr;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.can_rollback = false;
  g_status.rollback_version.clear();
  g_status.rollback_partition = other ? other->label : "";
  if (!other) return;
  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(other, &desc) != ESP_OK) return;
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(other, &st);
  g_status.rollback_version = desc.version;
  g_status.can_rollback = st != ESP_OTA_IMG_INVALID && st != ESP_OTA_IMG_ABORTED;
}

// GET /repos/<repo>/releases/latest: the tag, the notes and the two asset URLs.
bool do_check(std::string &error) {
  net::fetch::Request req;
  req.url = std::string("https://api.github.com/repos/") + CONFIG_P64_OTA_GITHUB_REPO + "/releases/latest";
  req.headers.push_back({"Accept", "application/vnd.github+json"});
  req.headers.push_back({"X-GitHub-Api-Version", "2022-11-28"});
  req.timeout_ms = 20000;
  net::fetch::Result res;
  if (!net::fetch::perform(req, res)) {
    error = "GitHub unreachable: " + std::string(esp_err_to_name(res.error));
    return false;
  }
  if (res.status == 404) {
    error = std::string("no release published at github.com/") + CONFIG_P64_OTA_GITHUB_REPO;
    return false;
  }
  if (res.status != 200) {
    error = "GitHub answered " + std::to_string(res.status);
    return false;
  }
  cJSON *root = cJSON_ParseWithLength(reinterpret_cast<const char *>(res.body.data()), res.body.size());
  if (!root) {
    error = "release JSON unreadable";
    return false;
  }
  const cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
  const cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
  const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
  std::string version = tag && cJSON_IsString(tag) ? tag->valuestring : "";
  if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
  std::string notes = body && cJSON_IsString(body) ? body->valuestring : "";
  if (notes.size() > kNotesMax) notes = notes.substr(0, kNotesMax) + "...";
  std::string bin_url, sha_url;
  uint32_t size = 0;
  const std::string sha_name = std::string(CONFIG_P64_OTA_ASSET) + ".sha256";
  const cJSON *asset;
  cJSON_ArrayForEach(asset, assets) {
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
    const cJSON *sz = cJSON_GetObjectItemCaseSensitive(asset, "size");
    if (!name || !cJSON_IsString(name) || !url || !cJSON_IsString(url)) continue;
    if (std::strcmp(name->valuestring, CONFIG_P64_OTA_ASSET) == 0) {
      bin_url = url->valuestring;
      if (sz && cJSON_IsNumber(sz)) size = static_cast<uint32_t>(sz->valuedouble);
    } else if (sha_name == name->valuestring) {
      sha_url = url->valuestring;
    }
  }
  cJSON_Delete(root);
  if (version.empty()) {
    error = "release without a tag";
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.available_version = version;
  g_status.notes = notes;
  g_status.download_url = bin_url;
  g_status.sha256_url = sha_url;
  g_status.available_size = size;
  g_status.last_check_us = esp_timer_get_time();
  if (bin_url.empty()) {
    error = "release " + version + " has no " + CONFIG_P64_OTA_ASSET;
    return false;
  }
  return true;
}

bool fetch_sha256(const std::string &url, uint8_t out[32], std::string &error) {
  net::fetch::Request req;
  req.url = url;
  req.max_bytes = 4096;
  net::fetch::Result res;
  if (!net::fetch::perform(req, res) || res.status != 200) {
    error = "checksum download failed (" + std::to_string(res.status) + ")";
    return false;
  }
  std::string text = net::fetch::body_string(res);
  size_t start = 0;
  while (start < text.size() && !isxdigit(static_cast<unsigned char>(text[start]))) ++start;
  if (!hex_to_bin(text.substr(start), out)) {
    error = "checksum file unreadable";
    return false;
  }
  return true;
}

bool partition_sha256(const esp_partition_t *part, size_t length, uint8_t out[32]) {
  std::vector<uint8_t> buf(4096);
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  for (size_t off = 0; off < length; off += buf.size()) {
    const size_t n = std::min(buf.size(), length - off);
    if (esp_partition_read(part, off, buf.data(), n) != ESP_OK) {
      mbedtls_sha256_free(&ctx);
      return false;
    }
    mbedtls_sha256_update(&ctx, buf.data(), n);
  }
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
  return true;
}

bool do_install(const std::string &url, const std::string &sha_hex, std::string &error) {
  uint8_t expected[32];
  bool have_expected = false;
  std::string sha_url;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    sha_url = g_status.sha256_url;
  }
  if (!sha_hex.empty()) {
    if (!hex_to_bin(sha_hex, expected)) {
      error = "sha256: 64 hex digits expected";
      return false;
    }
    have_expected = true;
  } else if (!sha_url.empty()) {
    if (!fetch_sha256(sha_url, expected, error)) return false;
    have_expected = true;
  }
  if (!have_expected) {
    error = "no SHA256 to verify against";
    return false;
  }
  set_state(State::Downloading);
  esp_http_client_config_t http = {};
  http.url = url.c_str();
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.timeout_ms = 30000;
  http.keep_alive_enable = true;
  http.buffer_size = 4096;
  http.buffer_size_tx = 1536;
  http.user_agent = net::fetch::user_agent();
  esp_https_ota_config_t cfg = {};
  cfg.http_config = &http;
  cfg.bulk_flash_erase = false;
  esp_https_ota_handle_t handle = nullptr;
  // One TLS session at a time (ADR 0009): the download takes the slot like any fetch.
  net::fetch::tls_lock();
  esp_err_t err = esp_https_ota_begin(&cfg, &handle);
  if (err != ESP_OK) {
    net::fetch::tls_unlock();
    error = std::string("download did not start: ") + esp_err_to_name(err);
    return false;
  }
  const int total = esp_https_ota_get_image_size(handle);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.image_size = total > 0 ? static_cast<uint32_t>(total) : 0;
    g_status.bytes_read = 0;
  }
  int64_t last_log = 0;
  while (true) {
    err = esp_https_ota_perform(handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    const int read = esp_https_ota_get_image_len_read(handle);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_status.bytes_read = read > 0 ? static_cast<uint32_t>(read) : 0;
    }
    const int64_t now = esp_timer_get_time();
    if (now - last_log > 2000000) {
      last_log = now;
      ESP_LOGI(TAG, "downloaded %d of %d bytes", read, total);
    }
  }
  if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
    esp_https_ota_abort(handle);
    net::fetch::tls_unlock();
    error = err != ESP_OK ? std::string("download failed: ") + esp_err_to_name(err) : "download incomplete";
    return false;
  }
  const int length = esp_https_ota_get_image_len_read(handle);
  set_state(State::Verifying);
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  uint8_t actual[32];
  if (!target || !partition_sha256(target, static_cast<size_t>(length), actual)) {
    esp_https_ota_abort(handle);
    net::fetch::tls_unlock();
    error = "could not read the written image back";
    return false;
  }
  if (std::memcmp(actual, expected, sizeof(actual)) != 0) {
    esp_https_ota_abort(handle);
    net::fetch::tls_unlock();
    error = "SHA256 mismatch: the image is not the one published";
    ESP_LOGE(TAG, "%s", error.c_str());
    return false;
  }
  err = esp_https_ota_finish(handle);  // validates the image and makes the slot bootable
  net::fetch::tls_unlock();
  if (err != ESP_OK) {
    error = std::string("image rejected: ") + esp_err_to_name(err);
    return false;
  }
  ESP_LOGI(TAG, "update written to %s (%d bytes, SHA256 verified); reboot to run it", target->label, length);
  return true;
}

void worker(void *) {
  Job job;
  std::string url, sha;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    job = g_job;
    url = g_job_url;
    sha = g_job_sha;
    g_job = Job::None;
  }
  std::string error;
  if (job == Job::Check) {
    set_state(State::Checking);
    if (do_check(error)) {
      std::string running, available;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        running = g_status.current_version;
        available = g_status.available_version;
      }
      const bool newer = version::is_newer(available, running);
      ESP_LOGI(TAG, "latest release %s; running %s: %s", available.c_str(), running.c_str(), newer ? "update available" : "up to date");
      set_state(newer ? State::Available : State::UpToDate);
    } else {
      ESP_LOGW(TAG, "check failed: %s", error.c_str());
      set_state(State::Error, error);
    }
  } else if (job == Job::Install) {
    if (do_install(url, sha, error)) {
      set_state(State::ReadyToReboot);
    } else {
      ESP_LOGW(TAG, "install failed: %s", error.c_str());
      set_state(State::Error, error);
    }
  }
  refresh_rollback();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_busy = false;
  }
  vTaskDelete(nullptr);
}

bool queue(Job job, const std::string &url, const std::string &sha) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_busy) return false;
    g_busy = true;
    g_job = job;
    g_job_url = url;
    g_job_sha = sha;
  }
  // Internal stack: the install writes flash (a PSRAM stack is not allowed to). The
  // task lives only for the job, so the internal RAM comes back afterwards.
  if (xTaskCreatePinnedToCore(worker, "ota", 8192, nullptr, 5, nullptr, 0) != pdPASS) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_busy = false;
    return false;
  }
  return true;
}

void on_timer(void *) {
  if (!system::settings().auto_update_check) return;
  if (!net::wifi::status().connected || !net::clock::synced()) return;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_status.state == State::Downloading || g_status.state == State::Verifying || g_status.state == State::ReadyToReboot) return;
  }
  if (!g_first_check_done) {
    g_first_check_done = true;
    esp_timer_stop(g_timer);
    esp_timer_start_periodic(g_timer, kCheckPeriodUs);
  }
  check_now();
}

}  // namespace

const char *state_name(State s) {
  switch (s) {
    case State::Idle: return "idle";
    case State::Checking: return "checking";
    case State::UpToDate: return "up_to_date";
    case State::Available: return "available";
    case State::Downloading: return "downloading";
    case State::Verifying: return "verifying";
    case State::ReadyToReboot: return "ready_to_reboot";
    case State::Error: return "error";
  }
  return "?";
}

bool start() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.current_version = esp_app_get_description()->version;
  }
  refresh_rollback();
  const esp_timer_create_args_t args = {on_timer, nullptr, ESP_TIMER_TASK, "ota_check", false};
  esp_timer_create(&args, &g_timer);
  esp_timer_start_periodic(g_timer, kFirstCheckUs);  // retried every 90 s until online, then every 12 h
  return true;
}

bool check_now() { return queue(Job::Check, "", ""); }

bool install(const std::string &url, const std::string &sha256_hex) {
  std::string target = url;
  if (target.empty()) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_status.state != State::Available || g_status.download_url.empty()) return false;
    target = g_status.download_url;
  }
  return queue(Job::Install, target, sha256_hex);
}

bool rollback(std::string &error) {
  refresh_rollback();
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = running ? esp_ota_get_next_update_partition(running) : nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!other || !g_status.can_rollback) {
      error = "the other slot holds no valid image";
      return false;
    }
  }
  const esp_err_t err = esp_ota_set_boot_partition(other);
  if (err != ESP_OK) {
    error = std::string("could not select the other slot: ") + esp_err_to_name(err);
    return false;
  }
  ESP_LOGW(TAG, "next boot from %s (%s)", other->label, g_status.rollback_version.c_str());
  return true;
}

Status status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}

cJSON *status_json() {
  const Status s = status();
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "state", state_name(s.state));
  cJSON_AddStringToObject(d, "current_version", s.current_version.c_str());
  cJSON_AddStringToObject(d, "available_version", s.available_version.c_str());
  cJSON_AddStringToObject(d, "notes", s.notes.c_str());
  cJSON_AddNumberToObject(d, "available_size", s.available_size);
  cJSON_AddStringToObject(d, "download_url", s.download_url.c_str());
  cJSON_AddBoolToObject(d, "sha256_published", !s.sha256_url.empty());
  cJSON_AddNumberToObject(d, "bytes_read", s.bytes_read);
  cJSON_AddNumberToObject(d, "image_size", s.image_size);
  cJSON_AddNumberToObject(d, "progress_percent", s.image_size ? 100.0 * s.bytes_read / s.image_size : 0);
  cJSON_AddStringToObject(d, "error", s.error.c_str());
  cJSON_AddNumberToObject(d, "last_check_age_s", s.last_check_us ? (esp_timer_get_time() - s.last_check_us) / 1e6 : -1);
  cJSON_AddBoolToObject(d, "can_rollback", s.can_rollback);
  cJSON_AddStringToObject(d, "rollback_version", s.rollback_version.c_str());
  cJSON_AddStringToObject(d, "rollback_partition", s.rollback_partition.c_str());
  cJSON_AddStringToObject(d, "repository", CONFIG_P64_OTA_GITHUB_REPO);
  cJSON_AddStringToObject(d, "asset", CONFIG_P64_OTA_ASSET);
  return d;
}

}  // namespace p64::ota

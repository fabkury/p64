#include "p64/ota/ota.hpp"

#include <cinttypes>
#include <cstring>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "p64/net/clock.hpp"
#include "p64/net/fetch.hpp"
#include "p64/net/wifi.hpp"
#include "p64/system/reliability.hpp"
#include "p64/system/settings.hpp"
#include "sdkconfig.h"
#include "release.hpp"
#include "version.hpp"

// Two tasks, because of two constraints: TLS wants a deep stack (12 KB) that must not
// come from the scarce internal heap, and flash writes must not run on a PSRAM stack
// (the cache is off while they happen). So the worker (PSRAM stack) checks the release,
// downloads the whole image into PSRAM through net::fetch and hashes it there; a small
// internal-stack task then copies the buffer into the other slot.
namespace p64::ota {
namespace {

constexpr const char *TAG = "ota";
constexpr uint64_t kCheckPeriodUs = 12ULL * 3600 * 1000000;
constexpr uint64_t kFirstCheckUs = 90ULL * 1000000;
constexpr size_t kNotesMax = 600;
constexpr size_t kImageMax = 6 * 1024 * 1024;  // the slot is 8 MB; the image is about 2 MB
constexpr size_t kFlashChunk = 16 * 1024;

enum class Job : uint8_t { None, Check, Install };

std::mutex g_mutex;
Status g_status;
bool g_busy = false;
Job g_job = Job::None;
std::string g_job_url, g_job_sha;
esp_timer_handle_t g_timer = nullptr;
bool g_first_check_done = false;

// The flash phase: the worker hands the PSRAM buffer over and waits.
struct FlashJob {
  const uint8_t *data;
  size_t size;
  const esp_partition_t *target;
  esp_err_t result;
  SemaphoreHandle_t done;
};

void set_state(State s, const std::string &error = "") {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.state = s;
  g_status.error = error;
}

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
  release::Release r;
  if (!release::parse(reinterpret_cast<const char *>(res.body.data()), res.body.size(), CONFIG_P64_OTA_ASSET, kNotesMax,
                      r, error)) {
    return false;
  }
  const std::string &version = r.version;
  const std::string &notes = r.notes;
  const std::string &bin_url = r.bin_url;
  const std::string &sha_url = r.sha_url;
  const uint32_t size = r.size;
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
  if (!release::digest_from_checksum_file(text, out)) {
    error = "checksum file unreadable";
    return false;
  }
  return true;
}

void sha256_of(const uint8_t *data, size_t len, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  for (size_t off = 0; off < len; off += 4096) mbedtls_sha256_update(&ctx, data + off, std::min<size_t>(4096, len - off));
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

// Internal-stack task: copies the verified image into the slot and makes it bootable.
void flash_task(void *arg) {
  auto *job = static_cast<FlashJob *>(arg);
  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(job->target, job->size, &handle);
  if (err == ESP_OK) {
    for (size_t off = 0; off < job->size && err == ESP_OK; off += kFlashChunk) {
      const size_t n = std::min(kFlashChunk, job->size - off);
      err = esp_ota_write(handle, job->data + off, n);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_status.bytes_read = static_cast<uint32_t>(off + n);
      }
    }
    if (err == ESP_OK) {
      err = esp_ota_end(handle);  // validates the image header and checksum
    } else {
      esp_ota_abort(handle);
    }
  }
  if (err == ESP_OK) err = esp_ota_set_boot_partition(job->target);
  if (err == ESP_OK) {
    refresh_rollback();  // flash reads: fine here, this task's stack is internal
    system::reliability::refresh_image_info();
  }
  job->result = err;
  xSemaphoreGive(job->done);
  vTaskDelete(nullptr);
}

bool do_install(const std::string &url, const std::string &sha_hex, std::string &error) {
  uint8_t expected[32];
  std::string sha_url;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    sha_url = g_status.sha256_url;
  }
  if (!sha_hex.empty()) {
    if (!release::hex_to_bin(sha_hex, expected)) {
      error = "sha256: 64 hex digits expected";
      return false;
    }
  } else if (!sha_url.empty()) {
    if (!fetch_sha256(sha_url, expected, error)) return false;
  } else {
    error = "no SHA256 to verify against";
    return false;
  }
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (!target) {
    error = "no other slot";
    return false;
  }
  set_state(State::Downloading);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.bytes_read = 0;
    g_status.image_size = 0;
  }
  net::fetch::Request req;
  req.url = url;
  req.timeout_ms = 60000;
  req.max_bytes = kImageMax;
  int64_t last_log = 0;
  req.progress = [&last_log](size_t received, int64_t total) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_status.bytes_read = static_cast<uint32_t>(received);
      if (total > 0) g_status.image_size = static_cast<uint32_t>(total);
    }
    const int64_t now = esp_timer_get_time();
    if (now - last_log > 2000000) {
      last_log = now;
      ESP_LOGI(TAG, "downloaded %u of %lld bytes", static_cast<unsigned>(received), static_cast<long long>(total));
    }
  };
  net::fetch::Result res;  // the body lands in PSRAM (large allocations do)
  if (!net::fetch::perform(req, res) || res.error != ESP_OK) {
    error = std::string("download failed: ") + esp_err_to_name(res.error ? res.error : ESP_FAIL);
    return false;
  }
  if (res.status != 200) {
    error = "download answered " + std::to_string(res.status);
    return false;
  }
  if (res.body.size() < 64 * 1024 || res.body[0] != 0xE9) {  // an ESP image starts with 0xE9
    error = "that is not a firmware image";
    return false;
  }
  set_state(State::Verifying);
  uint8_t actual[32];
  sha256_of(res.body.data(), res.body.size(), actual);
  if (std::memcmp(actual, expected, sizeof(actual)) != 0) {
    error = "SHA256 mismatch: the image is not the one published";
    ESP_LOGE(TAG, "%s", error.c_str());
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.image_size = static_cast<uint32_t>(res.body.size());
    g_status.bytes_read = 0;
  }
  FlashJob job{res.body.data(), res.body.size(), target, ESP_FAIL, xSemaphoreCreateBinary()};
  // Flash writes need an internal stack; 4 KB is plenty for esp_ota_write from a buffer.
  if (xTaskCreatePinnedToCore(flash_task, "ota_flash", 4096, &job, 5, nullptr, 0) != pdPASS) {
    vSemaphoreDelete(job.done);
    error = "could not start the flash writer";
    return false;
  }
  xSemaphoreTake(job.done, portMAX_DELAY);
  vSemaphoreDelete(job.done);
  if (job.result != ESP_OK) {
    error = std::string("flash write failed: ") + esp_err_to_name(job.result);
    return false;
  }
  ESP_LOGI(TAG, "update written to %s (%u bytes, SHA256 verified); reboot to run it", target->label,
           static_cast<unsigned>(res.body.size()));
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
  // No refresh_rollback() here: this task's stack is in PSRAM and that is a flash read.
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
  // TLS needs a deep stack; it lives in PSRAM (no flash writes on this task) and only
  // for the job's duration.
  if (xTaskCreatePinnedToCoreWithCaps(worker, "ota", 12288, nullptr, 5, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
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
  // A private build (firmware/private present at build time): a public release would
  // drop the private parts, and the Update page says so before the install.
#ifdef CONFIG_P64_PRIVATE
  cJSON_AddBoolToObject(d, "private_build", true);
#else
  cJSON_AddBoolToObject(d, "private_build", false);
#endif
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

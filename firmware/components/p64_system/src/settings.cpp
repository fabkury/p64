#include "p64/system/settings.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "p64/system/event_bus.hpp"
#include "p64/system/flash_guard.hpp"

// The settings store: NVS persistence, the shared immutable view, change events. The
// document's rules (clamping, JSON) are in settings_model.cpp.
namespace p64::system {
namespace {

constexpr const char *TAG = "settings";
constexpr const char *kNamespace = "p64";
constexpr const char *kKey = "cfg";
constexpr size_t kMaxJson = 8 * 1024;

std::mutex g_mutex;  // guards g_settings and g_view; never held across the NVS write
std::mutex g_write_mutex;  // serialises settings_update() calls
Settings g_settings;
std::shared_ptr<const Settings> g_view = std::make_shared<const Settings>();
bool g_loaded = false;
// Under g_write_mutex: the live document as JSON, and whether NVS still lacks it.
std::string g_live_json;
bool g_dirty = false;
int64_t g_dirty_since_us = 0;
esp_timer_handle_t g_flush_timer = nullptr;
std::atomic<uint32_t> g_nvs_writes{0};  // since boot, for the tests (a burst must cost one)
constexpr int64_t kQuietUs = 300 * 1000;
constexpr int64_t kMaxDelayUs = 2 * 1000 * 1000;


bool nvs_write_impl(const std::string &json) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
    return false;
  }
  err = nvs_set_blob(h, kKey, json.data(), json.size());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(TAG, "nvs write: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool nvs_read_impl(std::string &json) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 0;
  esp_err_t err = nvs_get_blob(h, kKey, nullptr, &len);
  if (err != ESP_OK || len == 0 || len > kMaxJson) {
    nvs_close(h);
    return false;
  }
  json.resize(len);
  err = nvs_get_blob(h, kKey, json.data(), &len);
  nvs_close(h);
  return err == ESP_OK;
}

bool nvs_write(const std::string &json) { return on_internal_stack([&] { return nvs_write_impl(json); }); }
bool nvs_read(std::string &json) { return on_internal_stack([&] { return nvs_read_impl(json); }); }

}  // namespace

bool settings_init() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_settings = Settings{};
  std::string json;
  if (nvs_read(json)) {
    std::string error;
    if (!g_settings.apply_json(json.c_str(), error)) {
      ESP_LOGW(TAG, "stored settings unreadable (%s); using defaults", error.c_str());
      g_settings = Settings{};
    } else {
      ESP_LOGI(TAG, "settings loaded (%u bytes)", static_cast<unsigned>(json.size()));
    }
  } else {
    ESP_LOGI(TAG, "no stored settings; using defaults");
  }
  g_settings.clamp();
  g_view = std::make_shared<const Settings>(g_settings);
  g_loaded = true;
  g_live_json = g_settings.to_json();
  if (!g_flush_timer) {
    const esp_timer_create_args_t args = {[](void *) { settings_flush(); }, nullptr, ESP_TIMER_TASK, "settings", false};
    esp_timer_create(&args, &g_flush_timer);
    esp_register_shutdown_handler([] { settings_flush(); });  // esp_restart() writes what is pending
  }
  return true;
}

Settings settings() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_settings;
}

std::shared_ptr<const Settings> settings_view() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_view;
}

// The NVS write happens outside g_mutex: the player's overlay hook reads the settings
// on every frame, and holding the lock across a flash write stalled it (review of
// 2026-09-22). g_write_mutex keeps concurrent updates in order.
// The document takes effect at once and is written to NVS a moment later: kQuietUs after
// the last change, at most kMaxDelayUs after the first one not yet written, so a burst of
// changes costs one flash write (100 writes in 3 s were 100 before; decision D30). An
// update that changes nothing writes nothing and notifies nobody. An orderly restart
// writes what is pending (the shutdown handler); a power cut within the delay loses the
// last change, as it would have a moment earlier.
bool settings_update(const std::function<void(Settings &)> &mutate) {
  std::lock_guard<std::mutex> serial(g_write_mutex);
  Settings next;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    next = g_settings;
  }
  mutate(next);
  next.clamp();
  std::string json = next.to_json();
  if (json.size() > kMaxJson) {
    ESP_LOGE(TAG, "settings document too large (%u bytes)", static_cast<unsigned>(json.size()));
    return false;
  }
  if (json == g_live_json) return true;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings = next;
    g_view = std::make_shared<const Settings>(next);
  }
  g_live_json = std::move(json);
  const int64_t now = esp_timer_get_time();
  if (!g_dirty) g_dirty_since_us = now;
  g_dirty = true;
  if (g_flush_timer) {
    const int64_t latest = g_dirty_since_us + kMaxDelayUs - now;
    esp_timer_stop(g_flush_timer);
    esp_timer_start_once(g_flush_timer, static_cast<uint64_t>(std::max<int64_t>(std::min(kQuietUs, latest), 1000)));
  } else if (nvs_write(g_live_json)) {  // before settings_init: write through
    g_dirty = false;
  }
  publish(Event::SettingsChanged);
  return true;
}

bool settings_flush() {
  std::lock_guard<std::mutex> serial(g_write_mutex);
  if (!g_dirty) return true;
  if (!nvs_write(g_live_json)) {
    if (g_flush_timer) {
      esp_timer_stop(g_flush_timer);
      esp_timer_start_once(g_flush_timer, static_cast<uint64_t>(kMaxDelayUs));  // try again
    }
    return false;
  }
  g_dirty = false;
  ++g_nvs_writes;
  return true;
}

uint32_t settings_nvs_writes() { return g_nvs_writes.load(); }

bool settings_reset() {
  return settings_update([](Settings &s) { s = Settings{}; });
}

}  // namespace p64::system

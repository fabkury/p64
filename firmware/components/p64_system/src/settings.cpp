#include "p64/system/settings.hpp"

#include <mutex>

#include "esp_log.h"
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
bool settings_update(const std::function<void(Settings &)> &mutate) {
  std::lock_guard<std::mutex> serial(g_write_mutex);
  Settings next;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    next = g_settings;
  }
  mutate(next);
  next.clamp();
  const std::string json = next.to_json();
  if (json.size() > kMaxJson) {
    ESP_LOGE(TAG, "settings document too large (%u bytes)", static_cast<unsigned>(json.size()));
    return false;
  }
  if (!nvs_write(json)) return false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings = next;
    g_view = std::make_shared<const Settings>(next);
  }
  publish(Event::SettingsChanged);
  return true;
}

bool settings_reset() {
  return settings_update([](Settings &s) { s = Settings{}; });
}

}  // namespace p64::system

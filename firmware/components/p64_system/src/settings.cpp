#include "p64/system/settings.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "p64/system/event_bus.hpp"

namespace p64::system {
namespace {

constexpr const char *TAG = "settings";
constexpr const char *kNamespace = "p64";
constexpr const char *kKey = "cfg";
constexpr size_t kMaxJson = 8 * 1024;

std::mutex g_mutex;
Settings g_settings;
bool g_loaded = false;

template <typename T>
T clamp_to(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

bool valid_device_name(const std::string &n) {
  if (n.size() > 16) return false;
  if (!n.empty() && (n.front() == '-' || n.back() == '-')) return false;
  for (char c : n) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

// --- JSON helpers -------------------------------------------------------------

cJSON *obj(cJSON *parent, const char *name) {
  cJSON *o = cJSON_AddObjectToObject(parent, name);
  return o;
}

void put_rgb(cJSON *parent, const char *name, gfx::Rgb c) {
  cJSON *o = cJSON_AddObjectToObject(parent, name);
  cJSON_AddNumberToObject(o, "r", c.r);
  cJSON_AddNumberToObject(o, "g", c.g);
  cJSON_AddNumberToObject(o, "b", c.b);
}

const cJSON *sub(const cJSON *parent, const char *name) {
  const cJSON *o = cJSON_GetObjectItemCaseSensitive(parent, name);
  return (o && cJSON_IsObject(o)) ? o : nullptr;
}

template <typename T>
void get_num(const cJSON *o, const char *name, T &out) {
  if (!o) return;
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
  if (v && cJSON_IsNumber(v)) out = static_cast<T>(v->valuedouble);
}

void get_bool(const cJSON *o, const char *name, bool &out) {
  if (!o) return;
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
  if (v && cJSON_IsBool(v)) out = cJSON_IsTrue(v);
}

void get_str(const cJSON *o, const char *name, std::string &out, size_t max_len) {
  if (!o) return;
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
  if (v && cJSON_IsString(v) && v->valuestring) out = std::string(v->valuestring).substr(0, max_len);
}

void get_rgb(const cJSON *o, const char *name, gfx::Rgb &out) {
  const cJSON *c = sub(o, name);
  if (!c) return;
  int r = out.r, g = out.g, b = out.b;
  get_num(c, "r", r);
  get_num(c, "g", g);
  get_num(c, "b", b);
  out = gfx::Rgb{static_cast<uint8_t>(clamp_to(r, 0, 255)), static_cast<uint8_t>(clamp_to(g, 0, 255)),
                 static_cast<uint8_t>(clamp_to(b, 0, 255))};
}

// Enum by name, tolerant of unknown values (left unchanged).
template <typename E>
void get_enum(const cJSON *o, const char *name, E &out, const char *const *names, size_t count) {
  if (!o) return;
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
  if (!v || !cJSON_IsString(v) || !v->valuestring) return;
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(v->valuestring, names[i]) == 0) {
      out = static_cast<E>(i);
      return;
    }
  }
}

const char *const kPanelModes[] = {"quality", "photo"};
const char *const kMainStates[] = {"animation_show", "widget", "stream"};
const char *const kPickModes[] = {"random", "recency"};
const char *const kChannelSelects[] = {"stochastic", "swrr"};
const char *const kCorners[] = {"top_left", "top_right", "bottom_left", "bottom_right"};
const char *const kWidgets[] = {"clock", "weather", "temperature"};

}  // namespace

void Settings::clamp() {
  brightness = clamp_to<uint8_t>(brightness, 1, 255);
  brightness_ceiling = clamp_to<uint8_t>(brightness_ceiling, 1, 255);
  night.start_minutes = clamp_to<uint16_t>(night.start_minutes, 0, 24 * 60 - 1);
  night.end_minutes = clamp_to<uint16_t>(night.end_minutes, 0, 24 * 60 - 1);
  if (!gfx::valid_rotation(rotation)) rotation = 90;
  gain_r = clamp_to<uint8_t>(gain_r, 50, 100);
  gain_g = clamp_to<uint8_t>(gain_g, 50, 100);
  gain_b = clamp_to<uint8_t>(gain_b, 50, 100);
  boot_animation_ms = clamp_to<uint16_t>(boot_animation_ms, 0, 5000);
  if (auto_swap_seconds != 0) auto_swap_seconds = clamp_to<uint32_t>(auto_swap_seconds, 5, 86400);
  interlude_clock = clamp_to<uint8_t>(interlude_clock, 0, 100);
  interlude_weather = clamp_to<uint8_t>(interlude_weather, 0, 100);
  interlude_temperature = clamp_to<uint8_t>(interlude_temperature, 0, 100);
  stream_silence_ms = clamp_to<uint32_t>(stream_silence_ms, 500, 60000);
  if (ddp_port == 0) ddp_port = 4048;
  if (raw_udp_port == 0) raw_udp_port = 4064;
  tap_sensitivity = clamp_to<uint8_t>(tap_sensitivity, 1, 10);
  if (!valid_device_name(device_name)) device_name.clear();
  if (timezone.empty()) timezone = "UTC";
  if (ntp_server.empty()) ntp_server = "pool.ntp.org";
  if (card_root.empty() || card_root.front() != '/') card_root = "/p64";
  downloads_cap_mb = clamp_to<uint16_t>(downloads_cap_mb, 16, 1024);
  makapix_refresh_seconds = clamp_to<uint32_t>(makapix_refresh_seconds, 60, 86400);
  channel_cache_size = clamp_to<uint16_t>(channel_cache_size, 32, 4096);
}

std::string Settings::hostname() const { return device_name.empty() ? "p64" : "p64-" + device_name; }

std::string Settings::to_json() const {
  cJSON *root = cJSON_CreateObject();
  cJSON *d = obj(root, "display");
  cJSON_AddNumberToObject(d, "brightness", brightness);
  cJSON_AddNumberToObject(d, "brightness_ceiling", brightness_ceiling);
  cJSON *n = obj(d, "night");
  cJSON_AddBoolToObject(n, "enabled", night.enabled);
  cJSON_AddNumberToObject(n, "start_minutes", night.start_minutes);
  cJSON_AddNumberToObject(n, "end_minutes", night.end_minutes);
  cJSON_AddNumberToObject(n, "brightness", night.brightness);
  cJSON_AddStringToObject(d, "panel_mode", kPanelModes[static_cast<int>(panel_mode)]);
  cJSON_AddNumberToObject(d, "rotation", rotation);
  cJSON_AddBoolToObject(d, "rotation_auto", rotation_auto);
  put_rgb(d, "background", background);
  cJSON *g = obj(d, "gains");
  cJSON_AddNumberToObject(g, "r", gain_r);
  cJSON_AddNumberToObject(g, "g", gain_g);
  cJSON_AddNumberToObject(g, "b", gain_b);
  cJSON_AddNumberToObject(d, "boot_animation_ms", boot_animation_ms);

  cJSON *s = obj(root, "show");
  cJSON_AddStringToObject(s, "main_state", kMainStates[static_cast<int>(main_state)]);
  cJSON_AddNumberToObject(s, "auto_swap_seconds", auto_swap_seconds);
  cJSON_AddStringToObject(s, "pick_mode", kPickModes[static_cast<int>(pick_mode)]);
  cJSON_AddStringToObject(s, "channel_select", kChannelSelects[static_cast<int>(channel_select)]);
  cJSON *co = obj(s, "clock_overlay");
  cJSON_AddBoolToObject(co, "enabled", clock_overlay.enabled);
  cJSON_AddStringToObject(co, "font", clock_overlay.font.c_str());
  cJSON_AddStringToObject(co, "corner", kCorners[static_cast<int>(clock_overlay.corner)]);
  cJSON_AddBoolToObject(co, "h24", clock_overlay.h24);
  put_rgb(co, "colour", clock_overlay.colour);

  cJSON *w = obj(root, "widgets");
  cJSON_AddStringToObject(w, "widget", kWidgets[static_cast<int>(widget)]);
  cJSON *ip = obj(w, "interlude_percent");
  cJSON_AddNumberToObject(ip, "clock", interlude_clock);
  cJSON_AddNumberToObject(ip, "weather", interlude_weather);
  cJSON_AddNumberToObject(ip, "temperature", interlude_temperature);

  cJSON *st = obj(root, "stream");
  cJSON_AddBoolToObject(st, "takeover", stream_takeover);
  cJSON_AddNumberToObject(st, "silence_ms", stream_silence_ms);
  cJSON_AddBoolToObject(st, "ddp_enabled", ddp_enabled);
  cJSON_AddNumberToObject(st, "ddp_port", ddp_port);
  cJSON_AddBoolToObject(st, "raw_udp_enabled", raw_udp_enabled);
  cJSON_AddNumberToObject(st, "raw_udp_port", raw_udp_port);

  cJSON *in = obj(root, "inputs");
  cJSON_AddBoolToObject(in, "tap_enabled", tap_enabled);
  cJSON_AddNumberToObject(in, "tap_sensitivity", tap_sensitivity);

  cJSON *net = obj(root, "network");
  cJSON_AddStringToObject(net, "device_name", device_name.c_str());
  cJSON_AddStringToObject(net, "timezone", timezone.c_str());
  cJSON_AddStringToObject(net, "ntp_server", ntp_server.c_str());

  cJSON *sto = obj(root, "storage");
  cJSON_AddStringToObject(sto, "card_root", card_root.c_str());
  cJSON_AddNumberToObject(sto, "downloads_cap_mb", downloads_cap_mb);

  cJSON *mk = obj(root, "makapix");
  cJSON_AddNumberToObject(mk, "refresh_seconds", makapix_refresh_seconds);
  cJSON_AddNumberToObject(mk, "channel_cache_size", channel_cache_size);

  cJSON *up = obj(root, "updates");
  cJSON_AddBoolToObject(up, "auto_check", auto_update_check);

  char *text = cJSON_PrintUnformatted(root);
  std::string out = text ? text : "{}";
  cJSON_free(text);
  cJSON_Delete(root);
  return out;
}

bool Settings::apply_json(const char *json, std::string &error) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    error = "invalid JSON";
    return false;
  }
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    error = "expected an object";
    return false;
  }
  const cJSON *d = sub(root, "display");
  get_num(d, "brightness", brightness);
  get_num(d, "brightness_ceiling", brightness_ceiling);
  const cJSON *n = sub(d, "night");
  get_bool(n, "enabled", night.enabled);
  get_num(n, "start_minutes", night.start_minutes);
  get_num(n, "end_minutes", night.end_minutes);
  get_num(n, "brightness", night.brightness);
  get_enum(d, "panel_mode", panel_mode, kPanelModes, 2);
  get_num(d, "rotation", rotation);
  get_bool(d, "rotation_auto", rotation_auto);
  get_rgb(d, "background", background);
  const cJSON *g = sub(d, "gains");
  get_num(g, "r", gain_r);
  get_num(g, "g", gain_g);
  get_num(g, "b", gain_b);
  get_num(d, "boot_animation_ms", boot_animation_ms);

  const cJSON *s = sub(root, "show");
  get_enum(s, "main_state", main_state, kMainStates, 3);
  get_num(s, "auto_swap_seconds", auto_swap_seconds);
  get_enum(s, "pick_mode", pick_mode, kPickModes, 2);
  get_enum(s, "channel_select", channel_select, kChannelSelects, 2);
  const cJSON *co = sub(s, "clock_overlay");
  get_bool(co, "enabled", clock_overlay.enabled);
  get_str(co, "font", clock_overlay.font, 32);
  get_enum(co, "corner", clock_overlay.corner, kCorners, 4);
  get_bool(co, "h24", clock_overlay.h24);
  get_rgb(co, "colour", clock_overlay.colour);

  const cJSON *w = sub(root, "widgets");
  get_enum(w, "widget", widget, kWidgets, 3);
  const cJSON *ip = sub(w, "interlude_percent");
  get_num(ip, "clock", interlude_clock);
  get_num(ip, "weather", interlude_weather);
  get_num(ip, "temperature", interlude_temperature);

  const cJSON *st = sub(root, "stream");
  get_bool(st, "takeover", stream_takeover);
  get_num(st, "silence_ms", stream_silence_ms);
  get_bool(st, "ddp_enabled", ddp_enabled);
  get_num(st, "ddp_port", ddp_port);
  get_bool(st, "raw_udp_enabled", raw_udp_enabled);
  get_num(st, "raw_udp_port", raw_udp_port);

  const cJSON *in = sub(root, "inputs");
  get_bool(in, "tap_enabled", tap_enabled);
  get_num(in, "tap_sensitivity", tap_sensitivity);

  const cJSON *net = sub(root, "network");
  get_str(net, "device_name", device_name, 16);
  get_str(net, "timezone", timezone, 64);
  get_str(net, "ntp_server", ntp_server, 64);

  const cJSON *sto = sub(root, "storage");
  get_str(sto, "card_root", card_root, 64);
  get_num(sto, "downloads_cap_mb", downloads_cap_mb);

  const cJSON *mk = sub(root, "makapix");
  get_num(mk, "refresh_seconds", makapix_refresh_seconds);
  get_num(mk, "channel_cache_size", channel_cache_size);

  const cJSON *up = sub(root, "updates");
  get_bool(up, "auto_check", auto_update_check);

  cJSON_Delete(root);
  clamp();
  return true;
}

namespace {

bool nvs_write(const std::string &json) {
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

bool nvs_read(std::string &json) {
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
  g_loaded = true;
  return true;
}

Settings settings() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_settings;
}

bool settings_update(const std::function<void(Settings &)> &mutate) {
  std::string json;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    Settings next = g_settings;
    mutate(next);
    next.clamp();
    json = next.to_json();
    if (json.size() > kMaxJson) {
      ESP_LOGE(TAG, "settings document too large (%u bytes)", static_cast<unsigned>(json.size()));
      return false;
    }
    if (!nvs_write(json)) return false;
    g_settings = next;
  }
  publish(Event::SettingsChanged);
  return true;
}

bool settings_reset() {
  return settings_update([](Settings &s) { s = Settings{}; });
}

}  // namespace p64::system

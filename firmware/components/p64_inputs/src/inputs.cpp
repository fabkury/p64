#include "p64/inputs/inputs.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "orientation.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"
#include "p64/system/state_store.hpp"
#include "qmi8658.hpp"
#include "sdkconfig.h"
#include "tap.hpp"

namespace p64::inputs {
namespace {

constexpr const char *TAG = "inputs";
constexpr uint32_t kSampleMs = 4;  // 250 Hz polling of a 500 Hz sensor
constexpr const char *kCalKey = "imu_cal";  // "<angle>,<rotation>"

Hooks g_hooks;
std::mutex g_mutex;
TapDetector g_taps;
OrientationTracker g_orientation;
bool g_present = false;
std::atomic<bool> g_tap_enabled{true};
std::atomic<bool> g_auto{false};
float g_ax = 0, g_ay = 0, g_az = 0;
float g_peak = 0;                 // the largest impulse in the last window
int64_t g_peak_window_us = 0;
uint32_t g_singles = 0, g_doubles = 0, g_read_errors = 0, g_samples = 0;
int64_t g_last_event_us = 0;
std::string g_last_event;

void apply_settings(const system::Settings &s) {
  g_tap_enabled = s.tap_enabled;
  g_auto = s.rotation_auto;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_taps.set_sensitivity(s.tap_sensitivity);
}

void load_calibration() {
  std::string v;
  if (!system::state::get(kCalKey, v)) return;
  const size_t comma = v.find(',');
  if (comma == std::string::npos) return;
  const float angle = std::strtof(v.c_str(), nullptr);
  const int rotation = std::atoi(v.c_str() + comma + 1);
  g_orientation.calibrate(angle, static_cast<uint16_t>(rotation), CONFIG_P64_IMU_ROTATION_SIGN);
  ESP_LOGI(TAG, "auto-rotation calibrated: gravity at %.0f degrees means rotation %d", static_cast<double>(angle), rotation);
}

void task(void *) {
  TickType_t wake = xTaskGetTickCount();
  while (true) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kSampleMs));
    float ax, ay, az;
    if (!qmi8658::read(ax, ay, az)) {
      ++g_read_errors;
      continue;
    }
    const int64_t now_us = esp_timer_get_time();
    const auto t_ms = static_cast<uint32_t>(now_us / 1000);
    Tap tap = Tap::None;
    bool rotated = false;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      ++g_samples;
      g_ax = ax;
      g_ay = ay;
      g_az = az;
      tap = g_taps.feed(t_ms, ax, ay, az);
      if (now_us - g_peak_window_us > 2000000) {
        g_peak = g_taps.take_peak();
        g_peak_window_us = now_us;
      }
      rotated = g_orientation.feed(t_ms, ax, ay, az);
    }
    if (tap != Tap::None) {
      const bool enabled = g_tap_enabled.load();
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (tap == Tap::Single) ++g_singles; else ++g_doubles;
        g_last_event = tap == Tap::Single ? "single" : "double";
        g_last_event_us = now_us;
      }
      ESP_LOGI(TAG, "%s tap%s", tap == Tap::Single ? "single" : "double", enabled ? "" : " (gestures off)");
      if (enabled) {
        if (tap == Tap::Single && g_hooks.next) g_hooks.next();
        if (tap == Tap::Double && g_hooks.previous) g_hooks.previous();
      }
    }
    if (rotated) {
      ESP_LOGI(TAG, "orientation: rotation %u%s", g_orientation.rotation(), g_auto.load() ? "" : " (auto off)");
      if (g_auto.load() && g_hooks.rotation_changed) g_hooks.rotation_changed();
    }
  }
}

}  // namespace

bool start(const Hooks &hooks) {
  g_hooks = hooks;
  apply_settings(system::settings());
  system::subscribe(system::Event::SettingsChanged, [](const system::Message &) { apply_settings(system::settings()); });
  if (!qmi8658::init()) return false;
  g_present = true;
  load_calibration();
  // I2C reads and arithmetic only: the stack can live in PSRAM.
  xTaskCreatePinnedToCoreWithCaps(task, "imu", 4096, nullptr, 6, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return true;
}

bool imu_present() { return g_present; }

bool auto_rotation_resolved() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_orientation.resolved();
}

uint16_t auto_rotation() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_orientation.rotation();
}

bool calibrate_upright(uint16_t rotation) {
  if (!g_present) return false;
  float angle;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_samples < 200) return false;  // let the low-pass settle
    angle = g_orientation.angle_deg();
    g_orientation.calibrate(angle, rotation, CONFIG_P64_IMU_ROTATION_SIGN);
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f,%u", static_cast<double>(angle), rotation);
  system::state::set(kCalKey, buf);
  ESP_LOGI(TAG, "calibrated upright: gravity at %.1f degrees is rotation %u", static_cast<double>(angle), rotation);
  return true;
}

bool calibrated() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_orientation.calibrated();
}

cJSON *imu_json() {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "present", g_present);
  std::lock_guard<std::mutex> lock(g_mutex);
  cJSON_AddNumberToObject(d, "ax", g_ax);
  cJSON_AddNumberToObject(d, "ay", g_ay);
  cJSON_AddNumberToObject(d, "az", g_az);
  cJSON_AddNumberToObject(d, "gravity_angle_deg", g_orientation.angle_deg());
  cJSON_AddNumberToObject(d, "in_plane_g", g_orientation.in_plane_g());
  cJSON_AddBoolToObject(d, "calibrated", g_orientation.calibrated());
  cJSON_AddBoolToObject(d, "resolved", g_orientation.resolved());
  cJSON_AddNumberToObject(d, "auto_rotation", g_orientation.rotation());
  cJSON_AddBoolToObject(d, "auto_enabled", g_auto.load());
  cJSON_AddBoolToObject(d, "tap_enabled", g_tap_enabled.load());
  cJSON_AddNumberToObject(d, "tap_threshold_g", TapDetector::threshold_for(system::settings().tap_sensitivity));
  cJSON_AddNumberToObject(d, "peak_g", g_peak);
  cJSON_AddNumberToObject(d, "single_taps", g_singles);
  cJSON_AddNumberToObject(d, "double_taps", g_doubles);
  cJSON_AddStringToObject(d, "last_event", g_last_event.c_str());
  cJSON_AddNumberToObject(d, "last_event_age_s", g_last_event_us ? (esp_timer_get_time() - g_last_event_us) / 1e6 : -1);
  cJSON_AddNumberToObject(d, "samples", g_samples);
  cJSON_AddNumberToObject(d, "read_errors", g_read_errors);
  return d;
}

}  // namespace p64::inputs

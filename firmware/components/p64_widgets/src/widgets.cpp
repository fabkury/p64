#include "p64/widgets/widgets.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>

#include "analogue.hpp"
#include "clock_format.hpp"
#include "faces.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "p64/content/psram.hpp"
#include "p64/net/clock.hpp"
#include "p64/net/fetch.hpp"
#include "p64/net/wifi.hpp"
#include "p64/system/event_bus.hpp"
#include "shtc3.hpp"
#include "weather_icons.hpp"
#include "weather_model.hpp"

namespace p64::widgets {
namespace {

constexpr const char *TAG = "widgets";
constexpr int64_t kSecond = 1000000;
constexpr int64_t kSensorPeriodUs = 60 * kSecond;
constexpr int64_t kTrendWindowUs = 3600 * kSecond;
constexpr int64_t kWeatherRetryUs = 5 * 60 * kSecond;
constexpr size_t kWeatherMaxBytes = 24 * 1024;

using gfx::Frame;
using gfx::Rgb;

std::mutex g_mutex;

// --- the sensor ---------------------------------------------------------------------

struct Sample {
  int64_t at_us;
  float temperature;
};
Reading g_reading;
std::deque<Sample> g_samples;
bool g_sensor_present = false;

void sensor_task(void *) {
  g_sensor_present = shtc3::init();
  while (true) {
    float t = 0, h = 0;
    if (g_sensor_present && shtc3::read(t, h)) {
      const std::shared_ptr<const system::Settings> view = system::settings_view();
      const system::Settings &s = *view;
      const int64_t now = esp_timer_get_time();
      std::lock_guard<std::mutex> lock(g_mutex);
      g_reading.valid = true;
      g_reading.temperature_c = t + static_cast<float>(s.temperature.offset_temperature);
      g_reading.humidity = std::fmin(100.0f, std::fmax(0.0f, h + static_cast<float>(s.temperature.offset_humidity)));
      g_reading.sampled_us = now;
      g_samples.push_back(Sample{now, t});
      while (!g_samples.empty() && now - g_samples.front().at_us > kTrendWindowUs) g_samples.pop_front();
      const Sample &oldest = g_samples.front();
      const int64_t span = now - oldest.at_us;
      g_reading.trend_c_per_hour = span >= 30 * 60 * kSecond ? (t - oldest.temperature) * 3600.0f * kSecond / static_cast<float>(span) : 0.0f;
    } else if (g_sensor_present) {
      ESP_LOGW(TAG, "sensor read failed");
    }
    vTaskDelay(pdMS_TO_TICKS(g_sensor_present ? 60000 : 600000));
  }
}

// --- the weather --------------------------------------------------------------------

weather_model::Forecast g_forecast;
std::string g_weather_error;
int64_t g_weather_next_us = 0;
TaskHandle_t g_weather_task = nullptr;

void weather_task(void *) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    const system::Settings &s = *view;
    if (!s.weather.location_set) continue;
    if (!net::wifi::status().connected || !net::clock::synced()) continue;
    const int64_t now = esp_timer_get_time();
    const bool due = g_forecast.fetched_us == 0 ? now >= g_weather_next_us
                                                : now - g_forecast.fetched_us >= static_cast<int64_t>(s.weather.refresh_minutes) * 60 * kSecond;
    if (!due && now < g_weather_next_us) continue;
    if (!due) continue;
    net::fetch::Request req;
    req.url = weather_model::request_url(s.weather.latitude, s.weather.longitude, s.weather.imperial);
    req.max_bytes = kWeatherMaxBytes;
    req.headers.emplace_back("Accept", "application/json");
    net::fetch::Result r;
    net::fetch::perform(req, r);
    weather_model::Forecast f;
    std::string error;
    bool ok = r.status == 200 && r.error == ESP_OK;
    if (ok) {
      ok = weather_model::parse(reinterpret_cast<const char *>(r.body.data()), r.body.size(), f, error);
    } else {
      error = r.status ? "HTTP " + std::to_string(r.status) : std::string(esp_err_to_name(r.error));
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (ok) {
        f.fetched_us = now;
        g_forecast = f;
        g_weather_error.clear();
        ESP_LOGI(TAG, "weather: %.1f %s, code %d (%s), today %.0f/%.0f, %d more days", static_cast<double>(f.temperature),
                 f.imperial ? "F" : "C", f.code, icons::group_name(icons::group_for_code(f.code)),
                 static_cast<double>(f.today.max), static_cast<double>(f.today.min), f.day_count);
      } else {
        g_weather_error = error;
        ESP_LOGW(TAG, "weather: %s", error.c_str());
      }
      g_weather_next_us = now + (ok ? static_cast<int64_t>(s.weather.refresh_minutes) * 60 * kSecond : kWeatherRetryUs);
    }
  }
}

// --- drawing helpers ------------------------------------------------------------------

// The local time at a moment on the monotonic clock (frames are rendered ahead).
bool local_time_at(int64_t due_us, tm &out) {
  time_t t;
  if (!net::clock::now_utc(t)) return false;
  if (due_us > 0) {
    const int64_t delta = due_us - esp_timer_get_time();
    t += static_cast<time_t>(delta / kSecond);
  }
  localtime_r(&t, &out);
  return true;
}

// --- sources --------------------------------------------------------------------------

class ClockSource : public playback::FrameSource {
 public:
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(Frame &out, uint32_t &delay_ms, int64_t due_us) override {
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    const system::Settings &s = *view;
    tm t;
    const bool have = local_time_at(due_us, t);
    delay_ms = faces::draw_clock(out, s, have ? &t : nullptr);
    return true;
  }

 private:
  std::string name_ = "clock";
};

class WeatherSource : public playback::FrameSource {
 public:
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(Frame &out, uint32_t &delay_ms, int64_t) override {
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    const system::Settings &s = *view;
    weather_model::Forecast f;
    std::string error;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      f = g_forecast;
      error = g_weather_error;
    }
    delay_ms = 60000;
    faces::draw_weather(out, s, f, error, esp_timer_get_time());
    return true;
  }

 private:
  std::string name_ = "weather";
};

class TemperatureSource : public playback::FrameSource {
 public:
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(Frame &out, uint32_t &delay_ms, int64_t) override {
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    const system::Settings &s = *view;
    delay_ms = 10000;
    faces::draw_temperature(out, s, sensor());
    return true;
  }

 private:
  std::string name_ = "temperature";
};

template <typename T>
std::shared_ptr<T> psram_shared() {
  return std::allocate_shared<T>(content::PsramAllocator<T>());
}

}  // namespace

// --- public API ---------------------------------------------------------------------

bool start() {
  xTaskCreatePinnedToCoreWithCaps(sensor_task, "sensor", 4096, nullptr, 3, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // I2C only
  // The weather fetch runs TLS on this task, which never touches flash, so its stack can
  // live in PSRAM.
  xTaskCreatePinnedToCoreWithCaps(weather_task, "weather", 12288, nullptr, 3, &g_weather_task, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  system::subscribe(system::Event::SettingsChanged, [](const system::Message &) {
    if (g_weather_task) xTaskNotifyGive(g_weather_task);
  });
  system::subscribe(system::Event::TimeSynced, [](const system::Message &) {
    if (g_weather_task) xTaskNotifyGive(g_weather_task);
  });
  return true;
}

std::shared_ptr<playback::FrameSource> make(system::WidgetKind kind) {
  switch (kind) {
    case system::WidgetKind::Weather: return psram_shared<WeatherSource>();
    case system::WidgetKind::Temperature: return psram_shared<TemperatureSource>();
    case system::WidgetKind::Clock:
    default: return psram_shared<ClockSource>();
  }
}

const char *widget_name(system::WidgetKind kind) {
  switch (kind) {
    case system::WidgetKind::Weather: return "weather";
    case system::WidgetKind::Temperature: return "temperature";
    case system::WidgetKind::Clock:
    default: return "clock";
  }
}

uint32_t overlay_key() {
  const std::shared_ptr<const system::Settings> view = system::settings_view();
  const system::Settings &s = *view;
  if (!s.clock_overlay.enabled || !net::clock::synced()) return 0;
  tm t;
  if (!local_time_at(0, t)) return 0;
  return faces::overlay_key(s, t);
}

void draw_overlay(Frame &frame) {
  const std::shared_ptr<const system::Settings> view = system::settings_view();
  const system::Settings &s = *view;
  tm t;
  if (!s.clock_overlay.enabled || !local_time_at(0, t)) return;
  faces::draw_overlay(frame, s, t);
}

Reading sensor() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_reading;
}

cJSON *weather_json() {
  weather_model::Forecast f;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    f = g_forecast;
    error = g_weather_error;
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "valid", f.valid);
  cJSON_AddStringToObject(o, "error", error.c_str());
  if (!f.valid) return o;
  cJSON_AddNumberToObject(o, "age_s", static_cast<double>((esp_timer_get_time() - f.fetched_us) / kSecond));
  cJSON_AddNumberToObject(o, "temperature", f.temperature);
  cJSON_AddStringToObject(o, "units", f.imperial ? "imperial" : "metric");
  cJSON_AddNumberToObject(o, "humidity", f.humidity);
  cJSON_AddNumberToObject(o, "code", f.code);
  cJSON_AddStringToObject(o, "condition", icons::group_name(icons::group_for_code(f.code)));
  cJSON_AddBoolToObject(o, "is_day", f.is_day);
  cJSON_AddNumberToObject(o, "today_max", f.today.max);
  cJSON_AddNumberToObject(o, "today_min", f.today.min);
  cJSON *days = cJSON_AddArrayToObject(o, "days");
  for (int i = 0; i < f.day_count; ++i) {
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "weekday", clock_format::weekday_short(f.days[i].weekday));
    cJSON_AddStringToObject(d, "condition", icons::group_name(icons::group_for_code(f.days[i].code)));
    cJSON_AddNumberToObject(d, "max", f.days[i].max);
    cJSON_AddNumberToObject(d, "min", f.days[i].min);
    cJSON_AddItemToArray(days, d);
  }
  return o;
}

void weather_refresh_now() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_forecast.fetched_us = 0;
    g_weather_next_us = 0;
  }
  if (g_weather_task) xTaskNotifyGive(g_weather_task);
}

}  // namespace p64::widgets

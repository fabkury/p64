#include "p64/widgets/widgets.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>

#include "clock_format.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "p64/content/psram.hpp"
#include "p64/gfx/fonts.hpp"
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
constexpr int64_t kWeatherStaleUs = 6LL * 3600 * kSecond;  // spec 7.2: "no data" after 6 h without a refresh
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
      const system::Settings s = system::settings();
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
    const system::Settings s = system::settings();
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
  if (!net::clock::synced()) return false;
  time_t t = time(nullptr);
  if (due_us > 0) {
    const int64_t delta = due_us - esp_timer_get_time();
    t += static_cast<time_t>(delta / kSecond);
  }
  localtime_r(&t, &out);
  return true;
}

const gfx::fonts::Font &font_named(const std::string &name) {
  const gfx::fonts::Font *f = gfx::fonts::by_name(name);
  return f ? *f : gfx::fonts::default_font();
}

std::string temperature_text(float value, bool decimals) {
  char buf[16];
  if (decimals) {
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(value));
  } else {
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
  }
  return buf;
}

void draw_trend(Frame &f, int x, int y, float per_hour, Rgb colour) {
  // Up, down or flat: a small arrow in a 5x5 box.
  if (per_hour > 0.3f) {
    for (int i = 0; i < 3; ++i) f.fill_rect(x + 2 - i, y + i, 1 + 2 * i, 1, colour);
    f.fill_rect(x + 2, y + 3, 1, 2, colour);
  } else if (per_hour < -0.3f) {
    f.fill_rect(x + 2, y, 1, 2, colour);
    for (int i = 0; i < 3; ++i) f.fill_rect(x + i, y + 2 + i, 5 - 2 * i, 1, colour);
  } else {
    f.fill_rect(x, y + 2, 5, 1, colour);
    f.fill_rect(x + 3, y + 1, 1, 3, colour);
  }
}

// --- sources --------------------------------------------------------------------------

class ClockSource : public playback::FrameSource {
 public:
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(Frame &out, uint32_t &delay_ms, int64_t due_us) override {
    const system::Settings s = system::settings();
    const gfx::fonts::Font &font = font_named(s.clock.font);
    out.clear(s.clock.background);
    tm t;
    if (!local_time_at(due_us, t)) {
      gfx::fonts::draw_centred(out, font, 20, "--:--", s.clock.colour, 2);
      gfx::fonts::draw_centred(out, font, 42, "NO TIME", s.clock.colour, 1);
      delay_ms = 1000;
      return true;
    }
    const bool colon = !s.clock.blink_colon || (t.tm_sec % 2 == 0);
    std::string text = clock_format::time_text(t, s.clock.h24, s.clock.seconds, colon);
    const std::string mer = clock_format::meridiem(t, s.clock.h24);
    int scale = s.clock.scale;
    while (scale > 1 && gfx::fonts::width(font, text, scale) + (mer.empty() ? 0 : gfx::fonts::width(font, mer, 1) + 2) > Frame::width() - 2) --scale;
    const int th = gfx::fonts::cap_height(font, scale);
    const int total = gfx::fonts::width(font, text, scale) + (mer.empty() ? 0 : gfx::fonts::width(font, mer, 1) + 2);
    const int x = (Frame::width() - total) / 2;
    const int y = 14;
    gfx::fonts::draw(out, font, x, y, text, s.clock.colour, scale);
    if (!mer.empty()) gfx::fonts::draw(out, font, x + gfx::fonts::width(font, text, scale) + 2, y + th - gfx::fonts::cap_height(font, 1), mer, s.clock.colour, 1);
    gfx::fonts::draw_centred(out, font, y + th + 8, clock_format::date_text(t, s.clock.month_first), s.clock.colour, 1);
    // Until the next second when seconds or the blink show, else until the next minute.
    delay_ms = (s.clock.seconds || s.clock.blink_colon) ? 1000 : static_cast<uint32_t>((60 - t.tm_sec) * 1000);
    if (delay_ms > 60000) delay_ms = 60000;
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
    const system::Settings s = system::settings();
    const gfx::fonts::Font &font = gfx::fonts::default_font();
    const Rgb ink = s.clock.colour;
    const Rgb dim{140, 140, 160};
    out.clear(s.clock.background);
    weather_model::Forecast f;
    std::string error;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      f = g_forecast;
      error = g_weather_error;
    }
    delay_ms = 60000;
    const int64_t now = esp_timer_get_time();
    if (!s.weather.location_set) {
      gfx::fonts::draw_centred(out, font, 20, "WEATHER", ink, 1);
      gfx::fonts::draw_centred(out, font, 32, "SET A", dim, 1);
      gfx::fonts::draw_centred(out, font, 40, "LOCATION", dim, 1);
      return true;
    }
    if (!f.valid || now - f.fetched_us > kWeatherStaleUs) {
      gfx::fonts::draw_centred(out, font, 20, "WEATHER", ink, 1);
      gfx::fonts::draw_centred(out, font, 32, "NO DATA", dim, 1);
      if (!error.empty()) gfx::fonts::draw_centred(out, font, 44, error.substr(0, 10), dim, 1);
      return true;
    }
    const icons::Group group = icons::group_for_code(f.code);
    if (const icons::Icon *ic = icons::find(group, !f.is_day, 24)) icons::draw(out, *ic, 2, 3);
    const std::string temp = temperature_text(f.temperature, false);
    gfx::fonts::draw(out, font, 30, 3, temp, ink, 2);
    gfx::fonts::draw(out, font, 30 + gfx::fonts::width(font, temp, 2) + 2, 3, f.imperial ? "F" : "C", ink, 1);
    gfx::fonts::draw(out, font, 30, 18, "H " + temperature_text(f.today.max, false), dim, 1);
    gfx::fonts::draw(out, font, 30, 25, "L " + temperature_text(f.today.min, false), dim, 1);
    // The next days: a 21-pixel column each.
    for (int i = 0; i < f.day_count && i < 3; ++i) {
      const weather_model::Day &d = f.days[i];
      const int x0 = i * 21 + (i == 2 ? 1 : 0);
      const char *wd = clock_format::weekday_short(d.weekday);
      const std::string letter = wd[0] ? std::string(1, wd[0]) : "?";
      gfx::fonts::draw(out, font, x0 + 8, 31, letter, ink, 1);
      if (const icons::Icon *ic = icons::find(icons::group_for_code(d.code), false, 12)) icons::draw(out, *ic, x0 + 4, 37);
      const std::string hi = temperature_text(d.max, false), lo = temperature_text(d.min, false);
      gfx::fonts::draw(out, font, x0 + (21 - gfx::fonts::width(font, hi, 1)) / 2, 50, hi, ink, 1);
      gfx::fonts::draw(out, font, x0 + (21 - gfx::fonts::width(font, lo, 1)) / 2, 57, lo, dim, 1);
    }
    // The age of the data, small, when it is old.
    const int64_t age_min = (now - f.fetched_us) / (60 * kSecond);
    if (age_min >= 90) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%lldh", static_cast<long long>(age_min / 60));
      gfx::fonts::draw(out, font, 64 - gfx::fonts::width(font, buf, 1) - 1, 26, buf, dim, 1);
    }
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
    const system::Settings s = system::settings();
    const gfx::fonts::Font &font = gfx::fonts::default_font();
    const Rgb ink = s.clock.colour;
    const Rgb dim{140, 140, 160};
    out.clear(s.clock.background);
    delay_ms = 10000;
    const Reading r = sensor();
    if (!r.valid) {
      gfx::fonts::draw_centred(out, font, 22, "NO", ink, 2);
      gfx::fonts::draw_centred(out, font, 40, "SENSOR", dim, 1);
      return true;
    }
    const bool imperial = s.weather.imperial;
    const float value = imperial ? r.temperature_c * 9.0f / 5.0f + 32.0f : r.temperature_c;
    const std::string temp = temperature_text(value, true);
    const int w = gfx::fonts::width(font, temp, 2) + 2 + gfx::fonts::width(font, "C", 1);
    const int x = (Frame::width() - w) / 2;
    gfx::fonts::draw(out, font, x, 12, temp, ink, 2);
    gfx::fonts::draw(out, font, x + gfx::fonts::width(font, temp, 2) + 2, 12, imperial ? "F" : "C", ink, 1);
    char hum[16];
    std::snprintf(hum, sizeof(hum), "%d%% RH", static_cast<int>(std::lround(r.humidity)));
    gfx::fonts::draw_centred(out, font, 34, hum, dim, 1);
    if (s.temperature.trend) {
      const float trend = imperial ? r.trend_c_per_hour * 9.0f / 5.0f : r.trend_c_per_hour;
      draw_trend(out, 29, 46, trend, ink);
      char tb[16];
      std::snprintf(tb, sizeof(tb), "%+.1f/H", static_cast<double>(trend));
      gfx::fonts::draw_centred(out, font, 54, tb, dim, 1);
    }
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
  const system::Settings s = system::settings();
  if (!s.clock_overlay.enabled || !net::clock::synced()) return 0;
  tm t;
  if (!local_time_at(0, t)) return 0;
  // The minute plus the settings that shape the drawing.
  uint32_t key = static_cast<uint32_t>(t.tm_hour * 60 + t.tm_min + 1);
  key ^= static_cast<uint32_t>(s.clock_overlay.corner) << 12;
  key ^= (s.clock_overlay.h24 ? 1u : 0u) << 14;
  key ^= static_cast<uint32_t>(s.clock_overlay.colour.r ^ (s.clock_overlay.colour.g << 8) ^ (s.clock_overlay.colour.b << 16)) << 15;
  return key ? key : 1;
}

void draw_overlay(Frame &frame) {
  const system::Settings s = system::settings();
  tm t;
  if (!s.clock_overlay.enabled || !local_time_at(0, t)) return;
  const gfx::fonts::Font &font = font_named(s.clock_overlay.font);
  const std::string text = clock_format::time_text(t, s.clock_overlay.h24, false);
  const int w = gfx::fonts::width(font, text, 1);
  const int h = gfx::fonts::cap_height(font, 1);
  const int margin = 2;  // one pixel plus the outline
  int x = margin, y = margin;
  if (s.clock_overlay.corner == system::Corner::TopRight || s.clock_overlay.corner == system::Corner::BottomRight) x = Frame::width() - w - margin;
  if (s.clock_overlay.corner == system::Corner::BottomLeft || s.clock_overlay.corner == system::Corner::BottomRight) y = Frame::height() - h - margin;
  const Rgb outline = gfx::kBlack;
  gfx::fonts::draw(frame, font, x, y, text, s.clock_overlay.colour, 1, &outline);
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

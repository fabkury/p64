#include "p64/widgets/widgets.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <new>
#include <utility>
#include <sys/time.h>

#include "air_model.hpp"
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

// The air quality (spec 7.4): fetched after the forecast on the same task, with the
// weather's location and refresh, but only while somebody wants it (the Widget state
// shows it, its interlude is on, or an air source was drawn lately): a second TLS
// session every half hour is not spent on a widget nobody sees.
air_model::Air g_air;
std::string g_air_error;
int64_t g_air_next_us = 0;
std::atomic<int64_t> g_air_wanted_us{0};  // when an air source last drew
constexpr size_t kAirMaxBytes = 8 * 1024;
constexpr int64_t kAirWantedUs = 2 * 3600 * kSecond;

bool air_wanted(const system::Settings &s, int64_t now) {
  if (s.interlude_air != 0) return true;
  if (s.main_state == system::MainState::Widget && s.widget == system::WidgetKind::Air) return true;
  const int64_t drawn = g_air_wanted_us.load();
  return drawn != 0 && now - drawn < kAirWantedUs;
}

// One GET of a small JSON document; the error says why not.
bool fetch_json(const std::string &url, size_t max_bytes, net::fetch::Result &r, std::string &error) {
  net::fetch::Request req;
  req.url = url;
  req.max_bytes = max_bytes;
  req.headers.emplace_back("Accept", "application/json");
  net::fetch::perform(req, r);
  if (r.status == 200 && r.error == ESP_OK) return true;
  error = r.status ? "HTTP " + std::to_string(r.status) : std::string(esp_err_to_name(r.error));
  return false;
}

// What the last attempt of each fetch asked for (the weather task's own): a different
// request is fetched at once, and its failure drops the other place's numbers.
std::string g_weather_url, g_air_url;

void fetch_weather(const system::Settings &s, int64_t now, const std::string &url, bool changed) {
  net::fetch::Result r;
  weather_model::Forecast f;
  std::string error;
  const bool ok = fetch_json(url, kWeatherMaxBytes, r, error) &&
                  weather_model::parse(reinterpret_cast<const char *>(r.body.data()), r.body.size(), f, error);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok && changed) g_forecast = weather_model::Forecast{};
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

void fetch_air(const system::Settings &s, int64_t now, const std::string &url, bool changed) {
  net::fetch::Result r;
  // The series is 1 KB; parsed on this task's stack.
  air_model::Air a;
  std::string error;
  const bool ok = fetch_json(url, kAirMaxBytes, r, error) &&
                  air_model::parse(reinterpret_cast<const char *>(r.body.data()), r.body.size(), a, error);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok && changed) g_air = air_model::Air{};
  if (ok) {
    a.fetched_us = now;
    g_air = a;
    g_air_error.clear();
    ESP_LOGI(TAG, "air: US AQI %d, European AQI %d, UV %.1f, PM2.5 %.1f, PM10 %.1f, %d hours", a.us_aqi, a.eu_aqi,
             static_cast<double>(a.uv), static_cast<double>(a.pm2_5), static_cast<double>(a.pm10), a.hours);
  } else {
    g_air_error = error;
    ESP_LOGW(TAG, "air: %s", error.c_str());
  }
  g_air_next_us = now + (ok ? static_cast<int64_t>(s.weather.refresh_minutes) * 60 * kSecond : kWeatherRetryUs);
}

void weather_task(void *) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    const system::Settings &s = *view;
    if (!s.weather.location_set) continue;
    if (!net::wifi::status().connected || !net::clock::synced()) continue;
    int64_t now = esp_timer_get_time();
    int64_t fetched, next;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      fetched = g_forecast.fetched_us;
      next = g_weather_next_us;
    }
    std::string url = weather_model::request_url(s.weather.latitude, s.weather.longitude, s.weather.imperial);
    bool changed = url != g_weather_url;
    if (weather_model::fetch_due(changed, fetched, next, now, s.weather.refresh_minutes)) {
      g_weather_url = url;
      fetch_weather(s, now, url, changed);
    }
    now = esp_timer_get_time();
    if (!air_wanted(s, now)) continue;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      fetched = g_air.fetched_us;
      next = g_air_next_us;
    }
    url = air_model::request_url(s.weather.latitude, s.weather.longitude);
    changed = url != g_air_url;
    if (weather_model::fetch_due(changed, fetched, next, now, s.weather.refresh_minutes)) {
      g_air_url = url;
      fetch_air(s, now, url, changed);
    }
  }
}

// --- drawing helpers ------------------------------------------------------------------

// The local time at a moment on the monotonic clock (frames are rendered ahead), with the
// milliseconds into its second and the zone's offset from UTC (hours, DST included) when
// asked.
bool local_time_at(int64_t due_us, tm &out, int *millis = nullptr, float *tz_hours = nullptr) {
  time_t t;
  if (!net::clock::now_utc(t)) return false;
  timeval tv{};
  gettimeofday(&tv, nullptr);
  int64_t total_us = static_cast<int64_t>(tv.tv_sec) * kSecond + tv.tv_usec;
  if (due_us > 0) total_us += due_us - esp_timer_get_time();
  t = static_cast<time_t>(total_us / kSecond);
  localtime_r(&t, &out);
  if (millis) *millis = static_cast<int>((total_us % kSecond) / 1000);
  if (tz_hours) {
    tm utc;
    gmtime_r(&t, &utc);
    int diff = (out.tm_hour * 60 + out.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    if (out.tm_year != utc.tm_year || out.tm_yday != utc.tm_yday) {
      const bool later = out.tm_year > utc.tm_year || (out.tm_year == utc.tm_year && out.tm_yday > utc.tm_yday);
      diff += later ? 24 * 60 : -24 * 60;
    }
    *tz_hours = diff / 60.0f;
  }
  return true;
}

// --- sources --------------------------------------------------------------------------

class ClockSource : public playback::FrameSource {
 public:
  explicit ClockSource(int face) : face_(face) {}
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(Frame &out, uint32_t &delay_ms, int64_t due_us) override {
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    // An overridden face (a random interlude): the settings with that face, copied per
    // frame (a few strings; the faces draw at most 25 frames a second).
    system::Settings overridden;
    if (face_ >= 0 && face_ < system::kClockFaceCount) {
      overridden = *view;
      overridden.clock.face = static_cast<system::ClockFace>(face_);
    }
    const system::Settings &s = face_ >= 0 && face_ < system::kClockFaceCount ? overridden : *view;
    tm t;
    faces::ClockContext ctx;
    float tz_hours = 0;
    const bool have = local_time_at(due_us, t, &ctx.millis, &tz_hours);
    ctx.time = have ? &t : nullptr;
    // The horizon's place and weather: the weather widget's location and, when a forecast
    // is fresh, its current condition; without a location 40 N and solar time.
    if (s.weather.location_set) {
      ctx.sky.latitude = s.weather.latitude;
      ctx.sky.longitude = s.weather.longitude;
      ctx.sky.tz_hours = tz_hours;
    }
    if (s.clock.face == system::ClockFace::Horizon || s.clock.face == system::ClockFace::HorizonRd) {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_forecast.valid && esp_timer_get_time() - g_forecast.fetched_us <= faces::kWeatherStaleUs)
        faces::weather_to_sky(g_forecast.code, ctx.sky);
    }
    delay_ms = faces::draw_clock(out, s, ctx, state_);
    return true;
  }

 private:
  std::string name_ = "clock";
  int face_ = -1;
  faces::ClockState state_;  // the flip's animation
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

class AirSource : public playback::FrameSource {
 public:
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(Frame &out, uint32_t &delay_ms, int64_t) override {
    const std::shared_ptr<const system::Settings> view = system::settings_view();
    const system::Settings &s = *view;
    // PSRAM: the series is too large for the player task's stack to hold twice.
    if (!data_) data_ = std::allocate_shared<air_model::Air>(content::PsramAllocator<air_model::Air>());
    std::string error;
    const int64_t now = esp_timer_get_time();
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      *data_ = g_air;
      error = g_air_error;
    }
    // Drawing it is what asks for the data: the fetcher wakes for the first frame and
    // this source looks again every second until the reply is in.
    const bool first = g_air_wanted_us.exchange(now) == 0;
    if ((first || !data_->valid) && g_weather_task) xTaskNotifyGive(g_weather_task);
    time_t utc = 0;
    if (!net::clock::now_utc(utc)) utc = 0;
    faces::draw_air(out, s, *data_, error, now, static_cast<int64_t>(utc));
    delay_ms = data_->valid ? 60000 : 1000;
    return true;
  }

 private:
  std::string name_ = "air";
  std::shared_ptr<air_model::Air> data_;
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

template <typename T, typename... Args>
std::shared_ptr<T> psram_shared(Args &&...args) {
  return std::allocate_shared<T>(content::PsramAllocator<T>(), std::forward<Args>(args)...);
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

std::shared_ptr<playback::FrameSource> make(system::WidgetKind kind, int face) {
  switch (kind) {
    case system::WidgetKind::Weather: return psram_shared<WeatherSource>();
    case system::WidgetKind::Temperature: return psram_shared<TemperatureSource>();
    case system::WidgetKind::Air: return psram_shared<AirSource>();
    case system::WidgetKind::Clock:
    default: return psram_shared<ClockSource>(face);
  }
}

const char *widget_name(system::WidgetKind kind) {
  switch (kind) {
    case system::WidgetKind::Weather: return "weather";
    case system::WidgetKind::Temperature: return "temperature";
    case system::WidgetKind::Air: return "air";
    case system::WidgetKind::Clock:
    default: return "clock";
  }
}

// The overlay as last built, touched only by the player task (which calls overlay_key()
// and then draw_overlay() for each frame); allocated once in PSRAM.
namespace {
faces::OverlaySprite *g_sprite = nullptr;
uint32_t g_sprite_key = 0;
}  // namespace

uint32_t overlay_key() {
  const std::shared_ptr<const system::Settings> view = system::settings_view();
  const system::Settings &s = *view;
  if (!s.clock_overlay.enabled || !net::clock::synced()) return 0;
  tm t;
  if (!local_time_at(0, t)) return 0;
  const uint32_t key = faces::overlay_key(s, t);
  if (key != g_sprite_key) {
    if (!g_sprite) {
      void *p = heap_caps_malloc(sizeof(faces::OverlaySprite), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!p) return 0;  // no overlay rather than a failed frame
      g_sprite = new (p) faces::OverlaySprite();
    }
    faces::build_overlay(*g_sprite, s, t);
    g_sprite_key = key;
  }
  return key;
}

void draw_overlay(Frame &frame) {
  if (g_sprite && g_sprite_key) faces::stamp_overlay(frame, *g_sprite);
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
  // The model's floats go out rounded to a tenth: widened to double as they are, 20.8 f
  // prints as 20.799999237060547 (seen in the web UI on 2026-09-26).
  const auto tenths = [](float v) { return std::round(static_cast<double>(v) * 10.0) / 10.0; };
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "valid", f.valid);
  cJSON_AddStringToObject(o, "error", error.c_str());
  if (!f.valid) return o;
  cJSON_AddNumberToObject(o, "age_s", static_cast<double>((esp_timer_get_time() - f.fetched_us) / kSecond));
  cJSON_AddNumberToObject(o, "temperature", tenths(f.temperature));
  cJSON_AddStringToObject(o, "units", f.imperial ? "imperial" : "metric");
  cJSON_AddNumberToObject(o, "humidity", f.humidity);
  cJSON_AddNumberToObject(o, "code", f.code);
  cJSON_AddStringToObject(o, "condition", icons::group_name(icons::group_for_code(f.code)));
  cJSON_AddBoolToObject(o, "is_day", f.is_day);
  cJSON_AddNumberToObject(o, "today_max", tenths(f.today.max));
  cJSON_AddNumberToObject(o, "today_min", tenths(f.today.min));
  cJSON *days = cJSON_AddArrayToObject(o, "days");
  for (int i = 0; i < f.day_count; ++i) {
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "weekday", clock_format::weekday_short(f.days[i].weekday));
    cJSON_AddStringToObject(d, "condition", icons::group_name(icons::group_for_code(f.days[i].code)));
    cJSON_AddNumberToObject(d, "max", tenths(f.days[i].max));
    cJSON_AddNumberToObject(d, "min", tenths(f.days[i].min));
    cJSON_AddItemToArray(days, d);
  }
  return o;
}

cJSON *air_json() {
  // Copied field by field under the lock: the series stays where it is.
  bool valid;
  int us, eu;
  float uv, pm2_5, pm10;
  int64_t fetched;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    valid = g_air.valid;
    us = g_air.us_aqi;
    eu = g_air.eu_aqi;
    uv = g_air.uv;
    pm2_5 = g_air.pm2_5;
    pm10 = g_air.pm10;
    fetched = g_air.fetched_us;
    error = g_air_error;
  }
  const auto tenths = [](float v) { return std::round(static_cast<double>(v) * 10.0) / 10.0; };
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "valid", valid);
  cJSON_AddStringToObject(o, "error", error.c_str());
  if (!valid) return o;
  cJSON_AddNumberToObject(o, "age_s", static_cast<double>((esp_timer_get_time() - fetched) / kSecond));
  cJSON_AddNumberToObject(o, "us_aqi", us);
  cJSON_AddNumberToObject(o, "european_aqi", eu);
  cJSON_AddStringToObject(o, "us_category", us < 0 ? "" : air_model::band(air_model::Scale::UsAqi, us).word);
  cJSON_AddStringToObject(o, "european_category", eu < 0 ? "" : air_model::band(air_model::Scale::EuropeanAqi, eu).word);
  cJSON_AddNumberToObject(o, "uv_index", tenths(uv));
  cJSON_AddStringToObject(o, "uv_category",
                          uv < 0 ? "" : air_model::band(air_model::Scale::Uv, static_cast<int>(static_cast<double>(uv) + 0.5)).word);
  cJSON_AddNumberToObject(o, "pm2_5", tenths(pm2_5));
  cJSON_AddNumberToObject(o, "pm10", tenths(pm10));
  return o;
}

void weather_refresh_now() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_forecast.fetched_us = 0;
    g_weather_next_us = 0;
    g_air.fetched_us = 0;
    g_air_next_us = 0;
  }
  if (g_weather_task) xTaskNotifyGive(g_weather_task);
}

}  // namespace p64::widgets

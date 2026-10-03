// The air widget's data (spec 7.4): Open-Meteo's air quality reply, the index bands and
// the graph's scale (pure, host-tested). tools/mock_air_widget.py is the design
// reference; the numbers and the words here are the mock's.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::widgets::air_model {

constexpr int kMaxHours = 48;  // two local days, so the day's bars survive midnight

struct Air {
  bool valid = false;
  int64_t fetched_us = 0;  // monotonic; 0 = never
  // The current values; -1 where the reply had none.
  int us_aqi = -1, eu_aqi = -1;
  float uv = -1, pm2_5 = -1, pm10 = -1;
  // The hourly series from the location's local midnight on: `first_hour_s` is that
  // midnight as seconds since 1970 on the location's own clock, `utc_offset_s` what to
  // add to UTC to get that clock.
  int64_t first_hour_s = 0;
  int32_t utc_offset_s = 0;
  int hours = 0;
  int16_t us_hours[kMaxHours];  // -1 = missing
  int16_t eu_hours[kMaxHours];
  float uv_hours[kMaxHours];  // negative = missing
};

// The Open-Meteo air quality URL for a location (current values and two days of hours).
std::string request_url(float latitude, float longitude);
// Parses the reply; false with a reason when the shape is wrong. fetched_us is not set.
bool parse(const char *json, size_t len, Air &out, std::string &error);
// "2026-10-03T00:00" to seconds since 1970 as if it were UTC; false when unparsable.
bool local_seconds(const char *iso, int64_t &out);
// The index of the hour `utc_now_s` falls in within the series, or -1 when before it.
int hour_index(const Air &a, int64_t utc_now_s);

struct Band {
  int upper;  // inclusive; the last band's is INT32_MAX
  const char *word;
  gfx::Rgb colour;
};
enum class Scale : uint8_t { UsAqi = 0, EuropeanAqi = 1, Uv = 2 };
const Band &band(Scale scale, int value);
// The graph's full height for a day whose highest hourly index is `peak`: the upper
// bound of the band the peak is in, at least the second band's; in the open last band
// the peak itself, at least one more band's width.
int scale_top(Scale scale, int peak);

}  // namespace p64::widgets::air_model

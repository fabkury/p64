// The weather data and the Open-Meteo reply parser (pure, host-tested).
#pragma once

#include <cstdint>
#include <string>

#include "weather_icons.hpp"

namespace p64::widgets::weather_model {

struct Day {
  int weekday = 0;  // 0 = Sunday
  int code = 0;
  float max = 0, min = 0;
};

struct Forecast {
  bool valid = false;
  int64_t fetched_us = 0;  // monotonic; 0 = never
  bool imperial = false;   // the units the numbers are in
  float temperature = 0;
  int humidity = 0;
  int code = 0;
  bool is_day = true;
  Day today;
  Day days[3];  // tomorrow and the two after
  int day_count = 0;
};

// The Open-Meteo forecast URL for a location (current conditions and four days).
std::string request_url(float latitude, float longitude, bool imperial);
// Parses the reply; false with a reason when the shape is wrong. fetched_us is not set.
bool parse(const char *json, size_t len, Forecast &out, std::string &error);
// "2026-09-19" (or with a time) to the weekday (0 = Sunday); -1 when unparsable.
int weekday_of(const std::string &iso_date);

}  // namespace p64::widgets::weather_model

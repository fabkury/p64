#include "weather_model.hpp"

#include <cstdio>

#include "cJSON.h"

namespace p64::widgets::weather_model {
namespace {

double num_at(const cJSON *arr, int i, double fallback) {
  const cJSON *v = arr ? cJSON_GetArrayItem(arr, i) : nullptr;
  return (v && cJSON_IsNumber(v)) ? v->valuedouble : fallback;
}

double num(const cJSON *obj, const char *key, double fallback) {
  const cJSON *v = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
  return (v && cJSON_IsNumber(v)) ? v->valuedouble : fallback;
}

}  // namespace

std::string request_url(float latitude, float longitude, bool imperial) {
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
                "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=4%s",
                static_cast<double>(latitude), static_cast<double>(longitude),
                imperial ? "&temperature_unit=fahrenheit" : "");
  return buf;
}

int weekday_of(const std::string &iso_date) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(iso_date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return -1;
  // Zeller-style weekday (Sakamoto's method), 0 = Sunday.
  static const int kOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 1 || m > 12) return -1;
  if (m < 3) --y;
  return (y + y / 4 - y / 100 + y / 400 + kOffsets[m - 1] + d) % 7;
}

bool parse(const char *json, size_t len, Forecast &out, std::string &error) {
  out = Forecast{};
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!root) {
    error = "not JSON";
    return false;
  }
  const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
  const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
  if (!current || !daily) {
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
    error = (reason && cJSON_IsString(reason)) ? reason->valuestring : "reply without current and daily";
    cJSON_Delete(root);
    return false;
  }
  out.temperature = static_cast<float>(num(current, "temperature_2m", 0));
  out.humidity = static_cast<int>(num(current, "relative_humidity_2m", 0));
  out.code = static_cast<int>(num(current, "weather_code", 0));
  out.is_day = num(current, "is_day", 1) != 0;
  const cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "current_units");
  const cJSON *unit = units ? cJSON_GetObjectItemCaseSensitive(units, "temperature_2m") : nullptr;
  out.imperial = unit && cJSON_IsString(unit) && unit->valuestring && unit->valuestring[1] == 'F';
  const cJSON *dates = cJSON_GetObjectItemCaseSensitive(daily, "time");
  const cJSON *codes = cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
  const cJSON *maxs = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
  const cJSON *mins = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
  const int n = dates ? cJSON_GetArraySize(dates) : 0;
  if (n < 1) {
    error = "no daily data";
    cJSON_Delete(root);
    return false;
  }
  for (int i = 0; i < n && i < 4; ++i) {
    const cJSON *date = cJSON_GetArrayItem(dates, i);
    Day day;
    day.weekday = (date && cJSON_IsString(date)) ? weekday_of(date->valuestring) : -1;
    day.code = static_cast<int>(num_at(codes, i, 0));
    day.max = static_cast<float>(num_at(maxs, i, 0));
    day.min = static_cast<float>(num_at(mins, i, 0));
    if (i == 0) {
      out.today = day;
    } else {
      out.days[out.day_count++] = day;
    }
  }
  out.valid = true;
  cJSON_Delete(root);
  return true;
}

}  // namespace p64::widgets::weather_model

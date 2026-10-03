#include "air_model.hpp"

#include <climits>
#include <cstdio>

#include "cJSON.h"

namespace p64::widgets::air_model {
namespace {

constexpr int kOpen = INT_MAX;

// The colours are the indexes' own, the darkest two of each lifted so they read as text
// on a black panel.
const Band kUs[] = {
    {50, "GOOD", {0, 228, 0}},       {100, "MODERATE", {255, 255, 0}},     {150, "SENSITIVE", {255, 126, 0}},
    {200, "UNHEALTHY", {255, 0, 0}}, {300, "V.UNHEALTHY", {170, 80, 190}}, {kOpen, "HAZARDOUS", {190, 0, 50}},
};
const Band kEu[] = {
    {20, "GOOD", {80, 240, 230}}, {40, "FAIR", {80, 204, 170}},     {60, "MODERATE", {240, 230, 65}},
    {80, "POOR", {255, 80, 80}},  {100, "VERY POOR", {190, 0, 60}}, {kOpen, "EXTREME", {160, 60, 170}},
};
const Band kUv[] = {
    {2, "LOW", {60, 200, 0}},        {5, "MODERATE", {247, 228, 0}},      {7, "HIGH", {248, 120, 0}},
    {10, "VERY HIGH", {230, 0, 30}}, {kOpen, "EXTREME", {150, 100, 255}},
};

struct Table {
  const Band *bands;
  int count;
};
Table table(Scale scale) {
  switch (scale) {
    case Scale::EuropeanAqi: return {kEu, 6};
    case Scale::Uv: return {kUv, 5};
    case Scale::UsAqi:
    default: return {kUs, 6};
  }
}

const cJSON *item(const cJSON *obj, const char *key) { return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr; }

double num(const cJSON *obj, const char *key, double fallback) {
  const cJSON *v = item(obj, key);
  return (v && cJSON_IsNumber(v)) ? v->valuedouble : fallback;
}

double num_at(const cJSON *arr, int i, double fallback) {
  const cJSON *v = arr ? cJSON_GetArrayItem(arr, i) : nullptr;
  return (v && cJSON_IsNumber(v)) ? v->valuedouble : fallback;
}

int rounded(double v) { return v < 0 ? -1 : static_cast<int>(v + 0.5); }

}  // namespace

std::string request_url(float latitude, float longitude) {
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
                "&current=us_aqi,european_aqi,pm2_5,pm10,uv_index"
                "&hourly=us_aqi,european_aqi,uv_index&timezone=auto&forecast_days=2",
                static_cast<double>(latitude), static_cast<double>(longitude));
  return buf;
}

bool local_seconds(const char *iso, int64_t &out) {
  int y = 0, m = 0, d = 0, hh = 0, mm = 0;
  if (!iso || std::sscanf(iso, "%d-%d-%dT%d:%d", &y, &m, &d, &hh, &mm) != 5) return false;
  if (m < 1 || m > 12 || d < 1 || d > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  // Days from 1970-01-01 (Howard Hinnant's days_from_civil).
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
  out = days * 86400 + hh * 3600 + mm * 60;
  return true;
}

int hour_index(const Air &a, int64_t utc_now_s) {
  const int64_t since = utc_now_s + a.utc_offset_s - a.first_hour_s;
  return since < 0 ? -1 : static_cast<int>(since / 3600);
}

bool parse(const char *json, size_t len, Air &out, std::string &error) {
  out = Air{};
  for (int i = 0; i < kMaxHours; ++i) {
    out.us_hours[i] = out.eu_hours[i] = -1;
    out.uv_hours[i] = -1;
  }
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!root) {
    error = "not JSON";
    return false;
  }
  const cJSON *current = item(root, "current");
  const cJSON *hourly = item(root, "hourly");
  const cJSON *times = item(hourly, "time");
  const cJSON *first = times ? cJSON_GetArrayItem(times, 0) : nullptr;
  if (!current || !first || !cJSON_IsString(first) || !local_seconds(first->valuestring, out.first_hour_s)) {
    const cJSON *reason = item(root, "reason");
    error = (reason && cJSON_IsString(reason)) ? reason->valuestring : "reply without current and hourly";
    cJSON_Delete(root);
    return false;
  }
  out.us_aqi = rounded(num(current, "us_aqi", -1));
  out.eu_aqi = rounded(num(current, "european_aqi", -1));
  out.uv = static_cast<float>(num(current, "uv_index", -1));
  out.pm2_5 = static_cast<float>(num(current, "pm2_5", -1));
  out.pm10 = static_cast<float>(num(current, "pm10", -1));
  out.utc_offset_s = static_cast<int32_t>(num(root, "utc_offset_seconds", 0));
  const cJSON *us = item(hourly, "us_aqi");
  const cJSON *eu = item(hourly, "european_aqi");
  const cJSON *uv = item(hourly, "uv_index");
  const int n = cJSON_GetArraySize(times);
  out.hours = n < kMaxHours ? n : kMaxHours;
  for (int i = 0; i < out.hours; ++i) {
    out.us_hours[i] = static_cast<int16_t>(rounded(num_at(us, i, -1)));
    out.eu_hours[i] = static_cast<int16_t>(rounded(num_at(eu, i, -1)));
    out.uv_hours[i] = static_cast<float>(num_at(uv, i, -1));
  }
  out.valid = true;
  cJSON_Delete(root);
  return true;
}

const Band &band(Scale scale, int value) {
  const Table t = table(scale);
  for (int i = 0; i < t.count; ++i) {
    if (value <= t.bands[i].upper) return t.bands[i];
  }
  return t.bands[t.count - 1];
}

int scale_top(Scale scale, int peak) {
  const Table t = table(scale);
  const int last = t.bands[t.count - 2].upper, before = t.bands[t.count - 3].upper;
  if (peak > last) return peak > 2 * last - before ? peak : 2 * last - before;
  for (int i = 1; i < t.count - 1; ++i) {
    if (peak <= t.bands[i].upper) return t.bands[i].upper;
  }
  return last;
}

}  // namespace p64::widgets::air_model

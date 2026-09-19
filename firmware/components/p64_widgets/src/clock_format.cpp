#include "clock_format.hpp"

#include <cstdio>

namespace p64::widgets::clock_format {

const char *weekday_short(int wday) {
  static const char *const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return (wday >= 0 && wday < 7) ? kDays[wday] : "";
}

const char *month_short(int mon) {
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return (mon >= 0 && mon < 12) ? kMonths[mon] : "";
}

std::string time_text(const tm &t, bool h24, bool seconds, bool colon) {
  int hour = t.tm_hour;
  char buf[16];
  const char sep = colon ? ':' : ' ';
  if (h24) {
    std::snprintf(buf, sizeof(buf), "%02d%c%02d", hour, sep, t.tm_min);
  } else {
    hour = hour % 12;
    if (hour == 0) hour = 12;
    std::snprintf(buf, sizeof(buf), "%2d%c%02d", hour, sep, t.tm_min);
  }
  std::string out = buf;
  if (seconds) {
    std::snprintf(buf, sizeof(buf), "%c%02d", sep, t.tm_sec);
    out += buf;
  }
  return out;
}

std::string meridiem(const tm &t, bool h24) {
  if (h24) return "";
  return t.tm_hour < 12 ? "AM" : "PM";
}

std::string date_text(const tm &t, bool month_first) {
  char buf[24];
  if (month_first) {
    std::snprintf(buf, sizeof(buf), "%s %s %d", weekday_short(t.tm_wday), month_short(t.tm_mon), t.tm_mday);
  } else {
    std::snprintf(buf, sizeof(buf), "%s %d %s", weekday_short(t.tm_wday), t.tm_mday, month_short(t.tm_mon));
  }
  return buf;
}

}  // namespace p64::widgets::clock_format

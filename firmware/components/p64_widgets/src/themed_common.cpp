#include "themed.hpp"

namespace p64::widgets::themed {

Moment Moment::from(const tm &t) {
  Moment m;
  m.hour = t.tm_hour;
  m.minute = t.tm_min;
  m.second = t.tm_sec;
  m.wday = t.tm_wday;
  m.mday = t.tm_mday;
  m.mon = t.tm_mon + 1;
  m.yday = t.tm_yday;
  m.year = t.tm_year + 1900;
  return m;
}

const char *weekday_name(int wday) {
  static const char *const kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return wday >= 0 && wday < 7 ? kNames[wday] : "???";
}

const char *month_name(int mon) {
  static const char *const kNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  return mon >= 1 && mon <= 12 ? kNames[mon - 1] : "???";
}

std::string date_text(const Moment &m, bool month_first) {
  const std::string day = std::to_string(m.mday);
  return month_first ? std::string(weekday_name(m.wday)) + " " + month_name(m.mon) + " " + day
                     : std::string(weekday_name(m.wday)) + " " + day + " " + month_name(m.mon);
}

std::string hour_text(const Moment &m, const Options &o) {
  const int h = o.h24 ? m.hour : m.h12();
  std::string s = std::to_string(h);
  if (s.size() < 2) s = (o.h24 ? "0" : " ") + s;
  return s;
}

bool colon_on(const Moment &m, const Options &o) { return !o.blink || m.second % 2 == 0; }

}  // namespace p64::widgets::themed

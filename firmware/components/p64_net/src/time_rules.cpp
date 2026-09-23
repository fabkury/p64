#include "p64/net/time_rules.hpp"

#include <cctype>
#include <cstring>

namespace p64::net::time_rules {
namespace {

// Days from 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm): no
// time zone and no mktime.
int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool equal_ci(const std::string &a, const char *b) {
  const size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

}  // namespace

bool parse_build_date(const char *text, int64_t &epoch) {
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (!text || std::strlen(text) != 11 || text[3] != ' ' || text[6] != ' ') return false;
  unsigned month = 0;
  for (unsigned i = 0; i < 12; ++i) {
    if (std::strncmp(text, kMonths[i], 3) == 0) month = i + 1;
  }
  if (month == 0) return false;
  const char d1 = text[4], d2 = text[5];
  if (!(d1 == ' ' || std::isdigit(static_cast<unsigned char>(d1))) || !std::isdigit(static_cast<unsigned char>(d2))) {
    return false;
  }
  const unsigned day = (d1 == ' ' ? 0 : static_cast<unsigned>(d1 - '0')) * 10 + static_cast<unsigned>(d2 - '0');
  int year = 0;
  for (int i = 7; i < 11; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    year = year * 10 + (text[i] - '0');
  }
  if (day < 1 || day > 31 || year < 1970) return false;
  epoch = days_from_civil(year, month, day) * kDay;
  return true;
}

int64_t ntp_floor(int64_t build_date) { return build_date - kDay; }

int64_t file_floor(int64_t build_date) { return ntp_floor(build_date) - kLongestRetention; }

bool accept_ntp(int64_t utc, int64_t floor) { return utc >= floor; }

bool from_dhcp(const Slot &slot) { return slot.name.empty() && slot.addressed; }

Plan plan(const Slots &actual, const std::string &setting, bool keep_dhcp) {
  Plan p;
  p.fill(Want::Empty);
  size_t next = 0;
  if (keep_dhcp && from_dhcp(actual[0])) p[next++] = Want::Keep;
  p[next++] = Want::Setting;
  for (size_t i = 0; i < kFallbacks.size() && next < kSlots; ++i) {
    if (equal_ci(setting, kFallbacks[i])) continue;
    p[next++] = i == 0 ? Want::Fallback0 : Want::Fallback1;
  }
  return p;
}

bool matches(const Slot &slot, Want want, const std::string &setting) {
  switch (want) {
    case Want::Keep:
      return true;
    case Want::Setting:
      return slot.name == setting;
    case Want::Fallback0:
      return slot.name == kFallbacks[0];
    case Want::Fallback1:
      return slot.name == kFallbacks[1];
    case Want::Empty:
      return slot.name.empty() && !slot.addressed;
  }
  return false;
}

std::vector<Repair> repairs(const Slots &actual, const std::string &setting, bool keep_dhcp) {
  const Plan p = plan(actual, setting, keep_dhcp);
  std::vector<Repair> out;
  for (size_t i = 0; i < kSlots; ++i) {
    if (!matches(actual[i], p[i], setting)) out.push_back({i, p[i]});
  }
  return out;
}

bool wait_warning_due(int64_t connected_s, int64_t warned_at_s) {
  if (connected_s < 60) return false;
  if (warned_at_s < 0) return true;
  return connected_s - warned_at_s >= 30 * 60;
}

}  // namespace p64::net::time_rules

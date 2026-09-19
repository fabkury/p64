#include "p64/system/rtc_codec.hpp"

namespace p64::system::rtc_codec {
namespace {

// Days from the civil epoch (1970-01-01) for a proleptic Gregorian date; the classic
// Howard Hinnant algorithm, so no time zone or mktime dependence.
int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t z, int &y, unsigned &m, unsigned &d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2;
}

}  // namespace

uint8_t to_bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
uint8_t from_bcd(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }

bool decode(const uint8_t regs[kRegisters], time_t &utc) {
  if (regs[0] & 0x80) return false;  // OS: the oscillator stopped since the last set
  const unsigned sec = from_bcd(regs[0] & 0x7F), min = from_bcd(regs[1] & 0x7F), hour = from_bcd(regs[2] & 0x3F);
  const unsigned day = from_bcd(regs[3] & 0x3F), month = from_bcd(regs[5] & 0x1F);
  const int year = 2000 + from_bcd(regs[6]);
  if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12 || year < 2024) return false;
  const int64_t days = days_from_civil(year, month, day);
  utc = static_cast<time_t>(days * 86400 + hour * 3600 + min * 60 + sec);
  return true;
}

void encode(time_t utc, uint8_t regs[kRegisters]) {
  int64_t days = utc / 86400;
  int64_t rem = utc % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  int y;
  unsigned m, d;
  civil_from_days(days, y, m, d);
  const unsigned weekday = static_cast<unsigned>((days + 4) % 7 + 7) % 7;  // 1970-01-01 was a Thursday (4)
  regs[0] = to_bcd(static_cast<uint8_t>(rem % 60));
  regs[1] = to_bcd(static_cast<uint8_t>((rem / 60) % 60));
  regs[2] = to_bcd(static_cast<uint8_t>(rem / 3600));
  regs[3] = to_bcd(static_cast<uint8_t>(d));
  regs[4] = static_cast<uint8_t>(weekday);
  regs[5] = to_bcd(static_cast<uint8_t>(m));
  regs[6] = to_bcd(static_cast<uint8_t>(y >= 2000 && y <= 2099 ? y - 2000 : 0));
}

}  // namespace p64::system::rtc_codec

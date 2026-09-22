// Host unit tests: the night rule and the RTC codec.
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


// --- night schedule and the RTC codec (spec 3.2, 10.2) -----------------------------

TEST_CASE("night") {
  using namespace p64::system;
  CHECK(night::in_window(22 * 60, 7 * 60, 23 * 60));       // crosses midnight: 23:00 in
  CHECK(night::in_window(22 * 60, 7 * 60, 3 * 60));        // 03:00 in
  CHECK(!night::in_window(22 * 60, 7 * 60, 7 * 60));       // 07:00 out (end exclusive)
  CHECK(!night::in_window(22 * 60, 7 * 60, 12 * 60));
  CHECK(night::in_window(22 * 60, 7 * 60, 22 * 60));       // start inclusive
  CHECK(night::in_window(13 * 60, 14 * 60, 13 * 60 + 30)); // same day
  CHECK(!night::in_window(13 * 60, 14 * 60, 14 * 60));
  CHECK(!night::in_window(13 * 60, 13 * 60, 13 * 60));     // empty window
  Settings s;
  bool night = false;
  s.brightness = 200;
  s.brightness_ceiling = 150;
  CHECK_EQ(night::effective_brightness(s, 12 * 60, night), 150);  // ceiling caps
  CHECK(!night);
  s.night.enabled = true;
  s.night.start_minutes = 22 * 60;
  s.night.end_minutes = 7 * 60;
  s.night.brightness = 40;
  CHECK_EQ(night::effective_brightness(s, 23 * 60, night), 40);
  CHECK(night);
  CHECK_EQ(night::effective_brightness(s, 12 * 60, night), 150);
  CHECK(!night);
  CHECK_EQ(night::effective_brightness(s, -1, night), 150);        // time unknown: no schedule
  CHECK(!night);
  s.night.brightness = 0;                                            // panel off
  CHECK_EQ(night::effective_brightness(s, 2 * 60, night), 0);
  CHECK(night);
  s.night.brightness = 255;
  CHECK_EQ(night::effective_brightness(s, 2 * 60, night), 150);    // still capped
}


TEST_CASE("rtc_codec") {
  using namespace p64::system;
  CHECK_EQ(rtc_codec::to_bcd(59), 0x59);
  CHECK_EQ(rtc_codec::from_bcd(0x47), 47);
  // 2026-09-19 22:15:30 UTC: 2026-09-19 00:00 UTC is 1789776000 (day 20715 since 1970).
  const time_t utc = 1789776000 + 22 * 3600 + 15 * 60 + 30;
  uint8_t regs[rtc_codec::kRegisters];
  rtc_codec::encode(utc, regs);
  CHECK_EQ(regs[0], 0x30); CHECK_EQ(regs[1], 0x15); CHECK_EQ(regs[2], 0x22);
  CHECK_EQ(regs[3], 0x19); CHECK_EQ(regs[5], 0x09); CHECK_EQ(regs[6], 0x26);
  CHECK_EQ(regs[4], 6);  // 2026-09-19 is a Saturday (0 = Sunday)
  time_t back = 0;
  CHECK(rtc_codec::decode(regs, back));
  CHECK_EQ(static_cast<long long>(back), static_cast<long long>(utc));
  regs[0] |= 0x80;  // oscillator stopped
  CHECK(!rtc_codec::decode(regs, back));
  regs[0] &= 0x7F;
  regs[6] = 0x20;   // 2020: implausible for a set clock
  CHECK(!rtc_codec::decode(regs, back));
  // Round trips across a year boundary and a leap day (2028-02-29).
  for (time_t t : {static_cast<time_t>(1767225599), static_cast<time_t>(1767225600), static_cast<time_t>(1835395200),
                   static_cast<time_t>(2000000000)}) {
    rtc_codec::encode(t, regs);
    CHECK(rtc_codec::decode(regs, back));
    CHECK_EQ(static_cast<long long>(back), static_cast<long long>(t));
  }
}

}  // namespace

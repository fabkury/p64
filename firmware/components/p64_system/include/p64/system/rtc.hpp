// p64 -- the on-board PCF85063A real-time clock (I2C 0x51): keeps UTC across reboots
// and power cuts (spec 10.2). NTP corrects it; at boot it seeds the system clock.
#pragma once

#include <ctime>

namespace p64::system::rtc {

// True when the chip answers (probed on first call).
bool present();
// False when the chip is absent, its oscillator stopped since the last set, or the
// date is implausible.
bool read(time_t &utc);
bool write(time_t utc);

}  // namespace p64::system::rtc

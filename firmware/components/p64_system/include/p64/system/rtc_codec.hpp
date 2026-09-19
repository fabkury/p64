// p64 -- the PCF85063A register image <-> UTC time, pure (host-tested). Seven registers
// from 0x04: seconds (bit 7 = OS, oscillator stopped: the time is not trustworthy),
// minutes, hours (24 h), days, weekdays, months, years (00..99 = 2000..2099), all BCD.
#pragma once

#include <cstdint>
#include <ctime>

namespace p64::system::rtc_codec {

constexpr uint8_t kFirstRegister = 0x04;
constexpr int kRegisters = 7;

// False when the OS flag is set or the fields are not a plausible date (year < 2024).
bool decode(const uint8_t regs[kRegisters], time_t &utc);
// Encodes `utc` (clears OS).
void encode(time_t utc, uint8_t regs[kRegisters]);

uint8_t to_bcd(uint8_t v);
uint8_t from_bcd(uint8_t v);

}  // namespace p64::system::rtc_codec

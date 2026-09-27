// p64 -- the Adafruit seesaw wire format (pure, host-tested): how a register is
// addressed, how the 32-bit values travel, what the NeoPixel and switch commands look
// like. The rotary encoder boards (Adafruit 5880, ATtiny8x7 seesaw) speak this; the
// I2C traffic itself is in seesaw.cpp. Register numbers from Adafruit_seesaw.h.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace p64::inputs::seesaw {

// Modules (the first address byte) and their registers (the second).
constexpr uint8_t kStatus = 0x00, kStatusHwId = 0x01, kStatusVersion = 0x02, kStatusSwRst = 0x7F;
constexpr uint8_t kGpio = 0x01, kGpioDirClrBulk = 0x03, kGpioBulk = 0x04, kGpioBulkSet = 0x05, kGpioPullEnSet = 0x0B;
constexpr uint8_t kNeoPixel = 0x0E, kNeoPixelPin = 0x01, kNeoPixelSpeed = 0x02, kNeoPixelBufLength = 0x03,
                  kNeoPixelBuf = 0x04, kNeoPixelShow = 0x05;
constexpr uint8_t kEncoder = 0x11, kEncoderPosition = 0x30, kEncoderDelta = 0x40;

// Hardware IDs the STATUS/HW_ID register answers with (Adafruit_seesaw.h).
constexpr uint8_t kHwIdSamd09 = 0x55, kHwIdTiny807 = 0x84, kHwIdTiny817 = 0x87, kHwIdTiny816 = 0x86,
                  kHwIdTiny1616 = 0x88, kHwIdTiny1617 = 0x89;
constexpr bool known_hw_id(uint8_t id) {
  return id == kHwIdSamd09 || id == kHwIdTiny807 || id == kHwIdTiny817 || id == kHwIdTiny816 || id == kHwIdTiny1616 ||
         id == kHwIdTiny1617;
}

// The 5880's fixed wiring: the switch on seesaw pin 24, the NeoPixel on pin 6, and the
// pause a read needs between the register address and the answer (Adafruit's default).
constexpr uint8_t kSwitchPin = 24, kNeoPixelPinNumber = 6;
constexpr uint32_t kReadDelayUs = 250;

// A 32-bit value on the wire is big-endian.
constexpr std::array<uint8_t, 4> be32(uint32_t v) {
  return {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
          static_cast<uint8_t>(v)};
}
constexpr uint32_t from_be32(const uint8_t *b) {
  return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | b[3];
}
constexpr int32_t from_be32_signed(const uint8_t *b) { return static_cast<int32_t>(from_be32(b)); }

// A GPIO bulk mask with one pin set.
constexpr uint32_t pin_mask(uint8_t pin) { return uint32_t{1} << pin; }
// In a BULK read, a pin with the pull-up enabled reads 1 released and 0 pressed.
constexpr bool switch_pressed(uint32_t bulk) { return (bulk & pin_mask(kSwitchPin)) == 0; }

// Register address frames.
constexpr std::array<uint8_t, 2> reg_address(uint8_t module, uint8_t reg) { return {module, reg}; }
constexpr std::array<uint8_t, 6> gpio_mask_command(uint8_t reg, uint32_t mask) {
  const auto m = be32(mask);
  return {kGpio, reg, m[0], m[1], m[2], m[3]};
}
// The software reset writes 0xFF to STATUS/SWRST.
constexpr std::array<uint8_t, 3> reset_command() { return {kStatus, kStatusSwRst, 0xFF}; }
// NeoPixel setup: pin, 800 kHz, one pixel of three bytes (GRB order on a WS2812).
constexpr std::array<uint8_t, 3> neopixel_pin_command() { return {kNeoPixel, kNeoPixelPin, kNeoPixelPinNumber}; }
constexpr std::array<uint8_t, 3> neopixel_speed_command() { return {kNeoPixel, kNeoPixelSpeed, 0x01}; }
constexpr std::array<uint8_t, 4> neopixel_length_command() { return {kNeoPixel, kNeoPixelBufLength, 0x00, 0x03}; }
// BUF takes a 16-bit big-endian offset then the bytes.
constexpr std::array<uint8_t, 7> neopixel_colour_command(uint8_t r, uint8_t g, uint8_t b) {
  return {kNeoPixel, kNeoPixelBuf, 0x00, 0x00, g, r, b};
}
constexpr std::array<uint8_t, 2> neopixel_show_command() { return {kNeoPixel, kNeoPixelShow}; }

}  // namespace p64::inputs::seesaw

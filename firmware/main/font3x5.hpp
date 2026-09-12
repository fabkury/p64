// p64 -- a 3x5 pixel digit font, enough for counters on a 64x64 panel.
#pragma once

#include <cstdint>

#include "display.hpp"

namespace p64 {

// Each digit is 5 rows; bit 2 is the left column, bit 0 the right column.
constexpr uint8_t kDigits3x5[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b001, 0b001, 0b001},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
};

inline void draw_digit(Frame &frame, int x, int y, unsigned digit, Rgb c) {
  if (digit > 9) return;
  for (int row = 0; row < 5; ++row) {
    const uint8_t bits = kDigits3x5[digit][row];
    for (int col = 0; col < 3; ++col) {
      if (bits & (0b100 >> col)) frame.set(x + col, y + row, c);
    }
  }
}

// Draws `value` right-aligned so that its last column is at x_right.
inline void draw_number_right(Frame &frame, int x_right, int y, unsigned value, Rgb c) {
  int x = x_right - 2;
  do {
    draw_digit(frame, x, y, value % 10, c);
    value /= 10;
    x -= 4;  // 3 columns + 1 gap
  } while (value > 0);
}

}  // namespace p64

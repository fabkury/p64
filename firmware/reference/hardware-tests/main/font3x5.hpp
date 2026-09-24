// p64 -- a 3x5 pixel font for digits, colon, dash and space: enough for clocks and
// counters on a 64x64 panel.
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

// Width in pixels of one glyph (without the 1-pixel gap that follows it).
inline int glyph_width_3x5(char ch) {
  if (ch >= '0' && ch <= '9') return 3;
  if (ch == '-') return 3;
  if (ch == ':' || ch == ' ') return 1;
  return 0;
}

// Width in pixels of a string, glyphs separated by 1-pixel gaps.
inline int text_width_3x5(const char *s) {
  int w = 0;
  for (; *s; ++s) {
    const int g = glyph_width_3x5(*s);
    if (g) w += g + (w ? 1 : 0);
  }
  return w;
}

// Draws a string of digits, ':', '-' and ' ' with its top-left corner at (x, y).
inline void draw_text_3x5(Frame &frame, int x, int y, const char *s, Rgb c) {
  for (; *s; ++s) {
    const char ch = *s;
    if (ch >= '0' && ch <= '9') {
      draw_digit(frame, x, y, static_cast<unsigned>(ch - '0'), c);
    } else if (ch == '-') {
      for (int col = 0; col < 3; ++col) frame.set(x + col, y + 2, c);
    } else if (ch == ':') {
      frame.set(x, y + 1, c);
      frame.set(x, y + 3, c);
    } else if (ch != ' ') {
      continue;
    }
    x += glyph_width_3x5(ch) + 1;
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

#include "p64/gfx/text.hpp"

#include <cstdint>

namespace p64::gfx::text {
namespace {

struct Glyph {
  char c;
  uint8_t rows[7];  // 5 bits each, most significant bit is the left column
};

// The classic 5x7 shapes.
constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'!', {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100}},
    {'"', {0b01010, 0b01010, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}},
    {'%', {0b11000, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b00011}},
    {'\'', {0b00100, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}},
    {'(', {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010}},
    {')', {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000}},
    {'+', {0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000}},
    {',', {0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b00100, 0b01000}},
    {'-', {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}},
    {'.', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}},
    {'/', {0b00000, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000}},
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},
    {':', {0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000}},
    {'=', {0b00000, 0b00000, 0b11111, 0b00000, 0b11111, 0b00000, 0b00000}},
    {'?', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100}},
    {'@', {0b01110, 0b10001, 0b10111, 0b10101, 0b10111, 0b10000, 0b01110}},
    {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {'C', {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},
    {'D', {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}},
    {'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'G', {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}},
    {'H', {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'I', {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'J', {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}},
    {'K', {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {'N', {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001}},
    {'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'Q', {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}},
    {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'V', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}},
    {'X', {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},
    {'Y', {0b10001, 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100}},
    {'Z', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},
    {'_', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111}},
};

constexpr uint8_t kBox[7] = {0b11111, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11111};

char normalise(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

const uint8_t *rows_of(char c) {
  c = normalise(c);
  for (const Glyph &g : kGlyphs) {
    if (g.c == c) return g.rows;
  }
  return nullptr;
}

}  // namespace

bool has_glyph(char c) { return rows_of(c) != nullptr; }

int draw_char(Frame &frame, int x, int y, char c, Rgb colour, int scale) {
  if (scale < 1) scale = 1;
  const uint8_t *rows = rows_of(c);
  if (!rows) rows = kBox;
  for (int row = 0; row < kGlyphHeight; ++row) {
    for (int col = 0; col < kGlyphWidth; ++col) {
      if (rows[row] & (1 << (kGlyphWidth - 1 - col))) {
        frame.fill_rect(x + col * scale, y + row * scale, scale, scale, colour);
      }
    }
  }
  return kGlyphWidth * scale;
}

int text_width(const std::string &s, int scale, int spacing) {
  if (s.empty()) return 0;
  if (scale < 1) scale = 1;
  return static_cast<int>(s.size()) * kGlyphWidth * scale + static_cast<int>(s.size() - 1) * spacing * scale;
}

int draw_text(Frame &frame, int x, int y, const std::string &s, Rgb colour, int scale, int spacing) {
  if (scale < 1) scale = 1;
  int cx = x;
  for (size_t i = 0; i < s.size(); ++i) {
    cx += draw_char(frame, cx, y, s[i], colour, scale);
    if (i + 1 < s.size()) cx += spacing * scale;
  }
  return cx - x;
}

int draw_centred(Frame &frame, int y, const std::string &s, Rgb colour, int scale, int spacing) {
  const int w = text_width(s, scale, spacing);
  return draw_text(frame, (Frame::width() - w) / 2, y, s, colour, scale, spacing);
}

}  // namespace p64::gfx::text

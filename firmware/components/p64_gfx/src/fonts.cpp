#include "p64/gfx/fonts.hpp"

#include <cstring>

namespace p64::gfx::fonts {
namespace {

const Glyph *glyph_of(const Font &font, char c) {
  const auto code = static_cast<unsigned char>(c);
  if (code < font.first || code > font.last) return nullptr;
  return &font.glyphs[code - font.first];
}

bool bit_at(const Font &font, const Glyph &g, int col, int row) {
  const int bytes_per_row = (g.width + 7) / 8;
  const uint8_t byte = font.bits[g.offset + row * bytes_per_row + col / 8];
  return (byte >> (7 - (col % 8))) & 1;
}

// Draws one glyph; `pass` 0 paints the outline halo, 1 the glyph itself.
void paint(Frame &frame, const Font &font, const Glyph &g, int x, int y, Rgb colour, int scale, bool halo) {
  for (int row = 0; row < g.height; ++row) {
    for (int col = 0; col < g.width; ++col) {
      if (!bit_at(font, g, col, row)) continue;
      const int px = x + (g.x_off + col) * scale;
      const int py = y + (g.y_off - font.top + row) * scale;
      if (halo) {
        frame.fill_rect(px - 1, py - 1, scale + 2, scale + 2, colour);
      } else {
        frame.fill_rect(px, py, scale, scale, colour);
      }
    }
  }
}

void paint_box(Frame &frame, int x, int y, int w, int h, Rgb colour, int scale) {
  frame.fill_rect(x, y, w * scale, scale, colour);
  frame.fill_rect(x, y + (h - 1) * scale, w * scale, scale, colour);
  frame.fill_rect(x, y, scale, h * scale, colour);
  frame.fill_rect(x + (w - 1) * scale, y, scale, h * scale, colour);
}

}  // namespace

const Font *by_name(const std::string &name) {
  for (size_t i = 0; i < kFontCount; ++i) {
    if (name == kFonts[i]->name) return kFonts[i];
  }
  return nullptr;
}

const Font &default_font() { return *kFonts[0]; }

int line_height(const Font &font, int scale) { return (font.bottom - font.top) * (scale < 1 ? 1 : scale); }

int cap_height(const Font &font, int scale) { return font.size * (scale < 1 ? 1 : scale); }

int width(const Font &font, const std::string &text, int scale) {
  if (scale < 1) scale = 1;
  int w = 0;
  for (char c : text) {
    const Glyph *g = glyph_of(font, c);
    w += (g ? g->advance : font.size) * scale;
  }
  return w;
}

int draw(Frame &frame, const Font &font, int x, int y, const std::string &text, Rgb colour, int scale,
         const Rgb *outline) {
  if (scale < 1) scale = 1;
  for (int pass = outline ? 0 : 1; pass < 2; ++pass) {
    int cx = x;
    for (char c : text) {
      const Glyph *g = glyph_of(font, c);
      if (!g) {
        if (pass == 1) paint_box(frame, cx, y, font.size - 1, font.size, colour, scale);
        cx += font.size * scale;
        continue;
      }
      if (g->width) paint(frame, font, *g, cx, y, pass == 0 ? *outline : colour, scale, pass == 0);
      cx += g->advance * scale;
    }
  }
  return width(font, text, scale);
}

int draw_centred(Frame &frame, const Font &font, int y, const std::string &text, Rgb colour, int scale,
                 const Rgb *outline) {
  const int w = width(font, text, scale);
  return draw(frame, font, (Frame::width() - w) / 2, y, text, colour, scale, outline);
}

}  // namespace p64::gfx::fonts

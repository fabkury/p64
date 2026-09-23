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

// The drawing is written once over a `fill(x, y, w, h, halo)` callback, so a frame and a
// mask (draw_mask) get the same pixels.

// Draws one glyph; `halo` paints the outline halo instead of the glyph itself.
template <typename Fill>
void paint(const Fill &fill, const Font &font, const Glyph &g, int x, int y, int scale, bool halo) {
  for (int row = 0; row < g.height; ++row) {
    for (int col = 0; col < g.width; ++col) {
      if (!bit_at(font, g, col, row)) continue;
      const int px = x + (g.x_off + col) * scale;
      const int py = y + (g.y_off - font.top + row) * scale;
      if (halo) {
        fill(px - 1, py - 1, scale + 2, scale + 2, true);
      } else {
        fill(px, py, scale, scale, false);
      }
    }
  }
}

template <typename Fill>
void paint_box(const Fill &fill, int x, int y, int w, int h, int scale) {
  fill(x, y, w * scale, scale, false);
  fill(x, y + (h - 1) * scale, w * scale, scale, false);
  fill(x, y, scale, h * scale, false);
  fill(x + (w - 1) * scale, y, scale, h * scale, false);
}

// Pass 0 (only with an outline) paints the halos, pass 1 the glyphs over them.
template <typename Fill>
void paint_text(const Fill &fill, const Font &font, int x, int y, const std::string &text, int scale, bool outline) {
  for (int pass = outline ? 0 : 1; pass < 2; ++pass) {
    int cx = x;
    for (char c : text) {
      const Glyph *g = glyph_of(font, c);
      if (!g) {
        if (pass == 1) paint_box(fill, cx, y, font.size - 1, font.size, scale);
        cx += font.size * scale;
        continue;
      }
      if (g->width) paint(fill, font, *g, cx, y, scale, pass == 0);
      cx += g->advance * scale;
    }
  }
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
  paint_text([&](int fx, int fy, int w, int h, bool halo) { frame.fill_rect(fx, fy, w, h, halo ? *outline : colour); },
             font, x, y, text, scale, outline != nullptr);
  return width(font, text, scale);
}

int draw_mask(uint8_t *mask, const Font &font, int x, int y, const std::string &text, int scale, bool outline) {
  if (scale < 1) scale = 1;
  paint_text(
      [&](int fx, int fy, int w, int h, bool halo) {
        const int x0 = fx < 0 ? 0 : fx, y0 = fy < 0 ? 0 : fy;
        const int x1 = fx + w > Frame::width() ? Frame::width() : fx + w;
        const int y1 = fy + h > Frame::height() ? Frame::height() : fy + h;
        for (int yy = y0; yy < y1; ++yy)
          for (int xx = x0; xx < x1; ++xx) mask[yy * Frame::width() + xx] = halo ? kMaskOutline : kMaskText;
      },
      font, x, y, text, scale, outline);
  return width(font, text, scale);
}

int draw_centred(Frame &frame, const Font &font, int y, const std::string &text, Rgb colour, int scale,
                 const Rgb *outline) {
  const int w = width(font, text, scale);
  return draw(frame, font, (Frame::width() - w) / 2, y, text, colour, scale, outline);
}

}  // namespace p64::gfx::fonts

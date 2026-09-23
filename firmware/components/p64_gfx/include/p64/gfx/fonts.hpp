// p64 -- the bundled pixel fonts as bitmap glyph tables (ADR 0008): Capital Hill 6 px,
// Everyday Slight 5 px, Everyday Standard 6 px, Everyday Typical 7 px and High Birth 9 px,
// all by VEXED (CC BY 4.0), rasterised from the TTFs by tools/gen_fonts.py into
// fonts_data.cpp. Integer scaling and an optional one-pixel
// outline; no kerning. Host-tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::gfx::fonts {

struct Glyph {
  uint8_t code;     // ASCII
  uint8_t advance;  // pen advance in pixels at scale 1
  uint8_t width;    // bitmap size (0 for blank glyphs)
  uint8_t height;
  int8_t x_off;     // bitmap position relative to the pen
  int8_t y_off;     // ... and to the line's top
  uint16_t offset;  // first byte of the bitmap in `bits` (rows of ceil(width/8) bytes, MSB left)
};

struct Font {
  const char *name;   // the settings value ("capital-hill")
  const char *label;  // the name shown in the web UI ("Capital Hill")
  uint8_t size;       // the native pixel size (cap height)
  bool overlay;       // offered for the clock overlay (the small ones); every font suits the Clock widget
  uint8_t top;       // the ink box: y_off of the tallest glyph ...
  uint8_t bottom;    // ... and the bottom of the deepest descender, relative to the line top
  uint8_t baseline;  // the baseline, relative to the line top
  uint8_t first, last;  // the ASCII range covered
  const Glyph *glyphs;
  const uint8_t *bits;
};

extern const Font *const kFonts[];
extern const size_t kFontCount;

// The font of that name ("capital-hill", "everyday-typical"); nullptr when unknown.
const Font *by_name(const std::string &name);
const Font &default_font();  // Capital Hill, the first of kFonts
// The ink height (tallest glyph to deepest descender) at a scale.
int line_height(const Font &font, int scale = 1);
// The height of digits and capitals at a scale (the ink box without descenders).
int cap_height(const Font &font, int scale = 1);
int width(const Font &font, const std::string &text, int scale = 1);
// Draws text with the ink box's top-left corner at (x, y); characters without a glyph draw
// as a box. `outline`, when given, draws a one-pixel outline of that colour first (the
// clock overlay uses it to read over any artwork). Returns the width drawn.
int draw(Frame &frame, const Font &font, int x, int y, const std::string &text, Rgb colour, int scale = 1,
         const Rgb *outline = nullptr);
// The same drawing into a panel-sized mask (row-major, Frame::width() * Frame::height()
// bytes): kMaskText where the text lands, kMaskOutline where only its outline does,
// other bytes untouched. draw() with colours applied over it gives the same pixels.
constexpr uint8_t kMaskOutline = 1;
constexpr uint8_t kMaskText = 2;
int draw_mask(uint8_t *mask, const Font &font, int x, int y, const std::string &text, int scale = 1,
              bool outline = false);
int draw_centred(Frame &frame, const Font &font, int y, const std::string &text, Rgb colour, int scale = 1,
                 const Rgb *outline = nullptr);

}  // namespace p64::gfx::fonts

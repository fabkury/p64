// p64 -- a built-in 5x7 pixel font for status screens (spec 6.4): digits, capitals and
// a little punctuation, drawn at an integer scale. Lowercase letters draw as capitals.
// The bundled artist fonts (Capital Hill and friends) come with the widgets milestone;
// this one is always there, needs no assets and is host-tested.
#pragma once

#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::gfx::text {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

// True when the character has a glyph (others draw as a small box).
bool has_glyph(char c);
// Draws one character with its top-left corner at (x, y); returns the advance in pixels
// (glyph width times scale, without spacing).
int draw_char(Frame &frame, int x, int y, char c, Rgb colour, int scale = 1);
// Draws a string; `spacing` is the gap between glyphs in unscaled pixels. Returns the
// width drawn.
int draw_text(Frame &frame, int x, int y, const std::string &s, Rgb colour, int scale = 1, int spacing = 1);
int text_width(const std::string &s, int scale = 1, int spacing = 1);
// Draws the string centred horizontally on the frame.
int draw_centred(Frame &frame, int y, const std::string &s, Rgb colour, int scale = 1, int spacing = 1);

}  // namespace p64::gfx::text

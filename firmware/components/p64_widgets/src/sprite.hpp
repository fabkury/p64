// The themed clock faces' drawing primitives (spec 7.1): RGBA sprites from clock_assets
// (and small RGBA canvases drawn at runtime) stamped onto a Frame with a 0/1 alpha, glyph
// cells out of a sheet, halos, Bresenham lines. Pure (host-tested); every pixel rule here
// is mirrored by tools/mock_clock_faces.py, which draws the test references.
#pragma once

#include <cstdint>

#include "clock_assets.hpp"
#include "p64/gfx/frame.hpp"

namespace p64::widgets::sprite {

using assets::Sprite;

// Alpha at or above this is ink; below it, nothing (no blending).
constexpr uint8_t kOpaque = 128;

// A rectangular window on RGBA pixels: a whole sprite, one cell of a sheet, a leaf of a
// tile. `stride` is in pixels.
struct View {
  const uint8_t *rgba;
  int stride;
  int w, h;
  const uint8_t *at(int x, int y) const { return rgba + (y * stride + x) * 4; }
  bool inked(int x, int y) const { return at(x, y)[3] >= kOpaque; }
  gfx::Rgb colour(int x, int y) const {
    const uint8_t *p = at(x, y);
    return {p[0], p[1], p[2]};
  }
};

View view(const Sprite &s);
// The `i`-th of `count` equal cells laid side by side 1 px apart (a digit or letter sheet).
View cell(const Sprite &sheet, int count, int i);
View sub(const View &v, int x0, int y0, int w, int h);

// The inked pixels copied to (x, y).
void blit(gfx::Frame &frame, const View &v, int x, int y);
// The inked pixels painted in `colour`.
void stamp(gfx::Frame &frame, const View &v, int x, int y, gfx::Rgb colour);
// The 3x3 neighbourhood of every inked pixel painted in `colour` (a glow behind a glyph).
void stamp_halo(gfx::Frame &frame, const View &v, int x, int y, gfx::Rgb colour);
// A one-pixel line, Bresenham.
void line(gfx::Frame &frame, int x0, int y0, int x1, int y1, gfx::Rgb colour);

// A small RGBA picture drawn at runtime (the flip's tile with its numerals) that reads
// back as a View.
template <int W, int H>
struct Canvas {
  uint8_t rgba[W * H * 4] = {};
  View view() const { return {rgba, W, W, H}; }
  void set(int x, int y, gfx::Rgb c) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t *p = rgba + (y * W + x) * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
  }
  // The sprite copied in (transparent pixels stay transparent).
  void paint(const View &v, int x, int y) {
    for (int yy = 0; yy < v.h; ++yy)
      for (int xx = 0; xx < v.w; ++xx)
        if (v.inked(xx, yy)) set(x + xx, y + yy, v.colour(xx, yy));
  }
  void stamp(const View &v, int x, int y, gfx::Rgb c) {
    for (int yy = 0; yy < v.h; ++yy)
      for (int xx = 0; xx < v.w; ++xx)
        if (v.inked(xx, yy)) set(x + xx, y + yy, c);
  }
  bool same_as(const Canvas &o) const {
    for (size_t i = 0; i < sizeof(rgba); ++i)
      if (rgba[i] != o.rgba[i]) return false;
    return true;
  }
};

}  // namespace p64::widgets::sprite

// p64 -- Frame: one panel-sized RGB888 picture in logical orientation, the unit every
// producer (player, widgets, streams, status screens) hands to the display.
#pragma once

#include <cstddef>
#include <cstdint>

#include "p64/gfx/geometry.hpp"

namespace p64::gfx {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  constexpr bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
  constexpr bool operator!=(const Rgb &o) const { return !(*this == o); }
};

constexpr Rgb kBlack{0, 0, 0};
constexpr Rgb kWhite{255, 255, 255};

class Frame {
 public:
  static constexpr int width() { return kPanelWidth; }
  static constexpr int height() { return kPanelHeight; }
  static constexpr size_t bytes() { return static_cast<size_t>(kPanelWidth) * kPanelHeight * 3; }

  void clear(Rgb c = kBlack);
  // Out-of-range coordinates are ignored, so callers draw without clipping.
  void set(int x, int y, Rgb c);
  Rgb get(int x, int y) const;  // black outside the frame
  void fill_rect(int x, int y, int w, int h, Rgb c);
  // Blends c over one pixel or a rectangle with opacity alpha (0 = untouched, 255 = set).
  void blend(int x, int y, Rgb c, uint8_t alpha);
  void blend_rect(int x, int y, int w, int h, Rgb c, uint8_t alpha);
  // Disc in continuous coordinates: pixel (i, j) covers [i, i+1) x [j, j+1).
  void fill_disc(float cx, float cy, float radius, Rgb c);
  void copy_from(const Frame &other);

  const uint8_t *data() const { return px_; }
  // Bulk access: width*height*3 bytes, row-major RGB888.
  uint8_t *pixels() { return px_; }

 private:
  uint8_t px_[kPanelWidth * kPanelHeight * 3] = {};
};

// Three 256-entry lookup tables applied per channel while copying to the panel; the
// identity when no gain is set.
struct ChannelLut {
  uint8_t r[256];
  uint8_t g[256];
  uint8_t b[256];
  // Percent gains 0..100 per channel (100 = identity).
  void set_gains(unsigned r_pct, unsigned g_pct, unsigned b_pct);
};

// Writes `src` rotated clockwise by `rotation` into `dst` (width*height*3 bytes, physical
// panel order), mapping every channel through `lut`. dst may not alias src.
void rotate_copy(const Frame &src, uint8_t *dst, Rotation rotation, const ChannelLut &lut);

}  // namespace p64::gfx

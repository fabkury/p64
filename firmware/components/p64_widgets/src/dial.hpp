// What the two analogue faces on painted dials draw with (face_bracket.cpp, face_station.cpp;
// p078): hands and markers as shapes along an axis, discs, thin hands, all in integers so
// that the pixels are those of tools/mock_clock_faces.py.
//   - a dial's centre is in half pixels (63 = 31.5);
//   - a direction is the sine table's (clock_assets' kSinQ14: Q14, tenths of a degree,
//     0 = 12 o'clock, clockwise);
//   - a shape is a profile: for a pixel, `t` (along the axis) and `s` (across it) in 64ths
//     of a pixel; it is inked where |s| is within the profile's half width at t, the
//     profile being straight lines between its (t, half width) points.
// Pure (host-tested).
#pragma once

#include <cstdint>

#include "clock_assets.hpp"
#include "p64/gfx/frame.hpp"

namespace p64::widgets::themed::dial {

struct Point {
  int t, w;  // 64ths of a pixel: the distance along the axis, the half width there
};
struct Profile {
  const Point *points;
  int count;
};
template <int N>
constexpr Profile profile(const Point (&points)[N]) {
  return {points, N};
}

// A shape's colour from where a pixel is across it: `lit` on one edge (s * 100 below
// -lit_percent * w; never when lit_percent is negative), `dark` on the other (s beyond
// half the width, when has_dark), `base` between.
struct Shade {
  gfx::Rgb base;
  int lit_percent = -1;
  gfx::Rgb lit{};
  bool has_dark = false;
  gfx::Rgb dark{};
  gfx::Rgb at(int s, int w) const {
    if (lit_percent >= 0 && s * 100 < -lit_percent * w) return lit;
    if (has_dark && s * 2 > w) return dark;
    return base;
  }
};

int sin_q(int tenths);  // any angle, Q14
int cos_q(int tenths);
// A Q14 value to the integer it rounds to (half up).
inline int q_round(int v) { return (v + (1 << (assets::kSinQ - 1))) >> assets::kSinQ; }
// Floor division (C++ truncates; the mock's // floors).
inline int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The half width at t, or -1 outside the shape.
int profile_width(const Profile &p, int t);
// The shape at `angle10` (tenths of a degree) around (cx2, cy2); `shadow` > 0 first
// darkens by that alpha the pixels one down and right of it that it does not cover.
void draw_shape(gfx::Frame &frame, int cx2, int cy2, int angle10, const Profile &p, const Shade &shade, uint8_t shadow = 0);
// The pixel at radius r2 (half pixels, negative = the other way) in a direction.
void polar_px(int cx2, int cy2, int angle10, int r2, int &x, int &y);
// The same point in half pixels (a disc's centre).
void polar_half(int cx2, int cy2, int angle10, int r2, int &x2, int &y2);
// A one-pixel hand: a Bresenham line from its tail to its tip.
void thin_hand(gfx::Frame &frame, int cx2, int cy2, int angle10, int length2, int tail2, gfx::Rgb colour);
// A disc centred at (cx2, cy2) half pixels, d2_max its squared radius in half pixels;
// `shade(dx, dy)` (half pixels from the centre) gives each pixel's colour.
template <typename ShadeFn>
void disc(gfx::Frame &frame, int cx2, int cy2, int d2_max, ShadeFn shade) {
  int r = 2;
  while (4 * (r - 2) * (r - 2) <= d2_max) ++r;
  for (int y = cy2 / 2 - r; y <= cy2 / 2 + r; ++y)
    for (int x = cx2 / 2 - r; x <= cx2 / 2 + r; ++x) {
      const int dx = 2 * x - cx2, dy = 2 * y - cy2;
      if (dx * dx + dy * dy <= d2_max) frame.set(x, y, shade(dx, dy));
    }
}

}  // namespace p64::widgets::themed::dial

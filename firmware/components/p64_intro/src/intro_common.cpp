#include "intro_common.hpp"

#include <algorithm>

namespace p64::intro {

void fill(Frame &f, const Context &ctx) { f.clear(ctx.background); }

void draw(Frame &f, const Layer &l, int ox, int oy) {
  for (int ly = 0; ly < l.h; ++ly)
    for (int lx = 0; lx < l.w; ++lx)
      if (inked(l, lx, ly)) f.set(l.x + ox + lx, l.y + oy + ly, colour(l, lx, ly));
}

void draw_alpha(Frame &f, const Layer &l, uint8_t alpha, int ox, int oy) {
  if (alpha == 0) return;
  if (alpha == 255) {
    draw(f, l, ox, oy);
    return;
  }
  for (int ly = 0; ly < l.h; ++ly)
    for (int lx = 0; lx < l.w; ++lx)
      if (inked(l, lx, ly)) f.blend(l.x + ox + lx, l.y + oy + ly, colour(l, lx, ly), alpha);
}

void draw_tinted(Frame &f, const Layer &l, Rgb tint, float k, int ox, int oy) {
  if (k <= 0.0f) {
    draw(f, l, ox, oy);
    return;
  }
  for (int ly = 0; ly < l.h; ++ly)
    for (int lx = 0; lx < l.w; ++lx)
      if (inked(l, lx, ly)) f.set(l.x + ox + lx, l.y + oy + ly, mix(colour(l, lx, ly), tint, k));
}

void draw_resampled(Frame &f, const Layer &l, int dx, int dy, int dw, int dh, float shade) {
  if (dw <= 0 || dh <= 0) return;
  for (int y = 0; y < dh; ++y) {
    const int ly = std::min<int>(l.h - 1, y * l.h / dh);
    for (int x = 0; x < dw; ++x) {
      const int lx = std::min<int>(l.w - 1, x * l.w / dw);
      if (!inked(l, lx, ly)) continue;
      const Rgb c = colour(l, lx, ly);
      f.set(dx + x, dy + y, shade >= 1.0f ? c : scale(c, shade));
    }
  }
}

uint32_t hash(uint32_t seed, uint32_t a, uint32_t b) {
  uint32_t x = seed ^ (a * 0x9E3779B1u) ^ (b * 0x85EBCA77u);
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// A rotation about the grey axis (1, 1, 1) of RGB space by the angle of `turns`: greys
// stay where they are, every other colour turns through the hue circle. Cheaper than a
// round trip through HSV (which cost 8 ms a frame on the device, 2026-10-20) and the
// same identity at 0.
HueTurn hue_turn(float turns) {
  HueTurn h;
  turns -= std::floor(turns);
  h.identity = turns == 0.0f;
  const float ang = turns * 6.28318530718f;
  const float cs = std::cos(ang), sn = std::sin(ang);
  const float a = cs + (1.0f - cs) / 3.0f;                 // the diagonal
  const float b = (1.0f - cs) / 3.0f - sn * 0.57735027f;  // one off-diagonal
  const float c = (1.0f - cs) / 3.0f + sn * 0.57735027f;  // the other
  h.m[0] = a; h.m[1] = b; h.m[2] = c;
  h.m[3] = c; h.m[4] = a; h.m[5] = b;
  h.m[6] = b; h.m[7] = c; h.m[8] = a;
  return h;
}

Rgb hue_rotate(Rgb col, const HueTurn &h) {
  if (h.identity) return col;
  const float r = col.r, g = col.g, b = col.b;
  auto clampb = [](float v) { return static_cast<uint8_t>(v < 0.0f ? 0 : (v > 255.0f ? 255 : std::lround(v))); };
  return {clampb(h.m[0] * r + h.m[1] * g + h.m[2] * b), clampb(h.m[3] * r + h.m[4] * g + h.m[5] * b),
          clampb(h.m[6] * r + h.m[7] * g + h.m[8] * b)};
}

}  // namespace p64::intro

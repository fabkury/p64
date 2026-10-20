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

Rgb hue_rotate(Rgb c, float turns) {
  turns -= std::floor(turns);
  if (turns == 0.0f) return c;
  const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
  const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
  const float d = mx - mn;
  if (d <= 0.0f) return c;  // grey: no hue to turn
  float h;
  if (mx == r) {
    h = (g - b) / d;
    if (h < 0.0f) h += 6.0f;
  } else if (mx == g) {
    h = 2.0f + (b - r) / d;
  } else {
    h = 4.0f + (r - g) / d;
  }
  h += turns * 6.0f;
  h -= 6.0f * std::floor(h / 6.0f);
  const float s = d / mx, v = mx;
  const int sector = static_cast<int>(h) % 6;
  const float fr = h - std::floor(h);
  const float p = v * (1.0f - s), q = v * (1.0f - s * fr), t = v * (1.0f - s * (1.0f - fr));
  float rr, gg, bb;
  switch (sector) {
    case 0: rr = v; gg = t; bb = p; break;
    case 1: rr = q; gg = v; bb = p; break;
    case 2: rr = p; gg = v; bb = t; break;
    case 3: rr = p; gg = q; bb = v; break;
    case 4: rr = t; gg = p; bb = v; break;
    default: rr = v; gg = p; bb = q; break;
  }
  return {byte(rr), byte(gg), byte(bb)};
}

}  // namespace p64::intro

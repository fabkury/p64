// The intro animations' shared vocabulary: the two logo layers and how to draw them whole,
// faded, filtered pixel by pixel or resampled into a rectangle; the per-boot hash; easings
// and sub-timelines; colour arithmetic. Every animation file includes this and nothing
// from ESP-IDF (host-tested).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "logo_assets.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/intro/intro.hpp"

namespace p64::intro {

using gfx::Frame;
using gfx::Rgb;
using logo::Layer;

// An animation: a name and a drawing of the frame at `u` in (0, 1), the animation's own
// time (the window up to kHoldFrom). `u` never reaches 0 or 1 here: the registry draws
// the background alone at 0 and the finished logo from 1 on, so an animation must simply
// converge on the finished logo as u -> 1 (settled by 0.95 is the house rule, so the
// handoff to the hold is invisible).
struct Anim {
  const char *name;
  void (*render)(Frame &frame, const Context &ctx, float u);
};

inline const Layer &L0() { return logo::kLayer0; }
inline const Layer &L1() { return logo::kLayer1; }

// --- layer pixels -----------------------------------------------------------------

inline bool inside(const Layer &l, int lx, int ly) { return lx >= 0 && ly >= 0 && lx < l.w && ly < l.h; }
inline bool inked(const Layer &l, int lx, int ly) { return l.rgba[(ly * l.w + lx) * 4 + 3] >= 128; }
inline Rgb colour(const Layer &l, int lx, int ly) {
  const uint8_t *p = l.rgba + (ly * l.w + lx) * 4;
  return {p[0], p[1], p[2]};
}
// The layer's pixel under canvas (x, y) when it sits at home + (ox, oy); false where the
// layer is transparent or absent.
inline bool sample(const Layer &l, int x, int y, Rgb &c, int ox = 0, int oy = 0) {
  const int lx = x - l.x - ox, ly = y - l.y - oy;
  if (!inside(l, lx, ly) || !inked(l, lx, ly)) return false;
  c = colour(l, lx, ly);
  return true;
}
// The layer's centre on the canvas (continuous: the centre of its box).
inline float centre_x(const Layer &l) { return l.x + l.w * 0.5f; }
inline float centre_y(const Layer &l) { return l.y + l.h * 0.5f; }

// --- drawing ----------------------------------------------------------------------

void fill(Frame &f, const Context &ctx);
// The layer at home + (ox, oy); pixels off the canvas are dropped.
void draw(Frame &f, const Layer &l, int ox = 0, int oy = 0);
// The layer blended over the frame at opacity `alpha` (255 = draw).
void draw_alpha(Frame &f, const Layer &l, uint8_t alpha, int ox = 0, int oy = 0);
// The layer tinted: its colours mixed towards `tint` by `k` (0 = the layer, 1 = flat tint).
void draw_tinted(Frame &f, const Layer &l, Rgb tint, float k, int ox = 0, int oy = 0);
// The layer resampled (nearest) into the rectangle (dx, dy, dw, dh) of the canvas, its
// colours scaled by `shade` (1 = as they are); a width or height of 0 draws nothing.
void draw_resampled(Frame &f, const Layer &l, int dx, int dy, int dw, int dh, float shade = 1.0f);
// The inked pixels (lx, ly) that `keep` accepts, at home + (ox, oy).
template <class Keep>
void draw_if(Frame &f, const Layer &l, Keep keep, int ox = 0, int oy = 0) {
  for (int ly = 0; ly < l.h; ++ly)
    for (int lx = 0; lx < l.w; ++lx)
      if (inked(l, lx, ly) && keep(lx, ly)) f.set(l.x + ox + lx, l.y + oy + ly, colour(l, lx, ly));
}
// Every inked pixel: fn(lx, ly, colour) decides where and how it lands.
template <class Fn>
void each(const Layer &l, Fn fn) {
  for (int ly = 0; ly < l.h; ++ly)
    for (int lx = 0; lx < l.w; ++lx)
      if (inked(l, lx, ly)) fn(lx, ly, colour(l, lx, ly));
}

// --- chance -----------------------------------------------------------------------

// A stateless hash of the seed and two keys (a pixel, a tile, a strip): the same answer
// every frame, so an element's fate is fixed for the whole animation.
uint32_t hash(uint32_t seed, uint32_t a, uint32_t b);
inline float unit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
inline float chance(uint32_t seed, uint32_t a, uint32_t b) { return unit(hash(seed, a, b)); }

// --- time and easing --------------------------------------------------------------

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// The progress of a sub-timeline that runs from `start` to `end` of u, clamped.
inline float window(float u, float start, float end) { return clamp01((u - start) / (end - start)); }
inline float smoothstep(float t) {
  t = clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}
inline float ease_out_cubic(float t) {
  t = clamp01(t);
  const float v = 1.0f - t;
  return 1.0f - v * v * v;
}
inline float ease_in_cubic(float t) {
  t = clamp01(t);
  return t * t * t;
}
inline float ease_in_quad(float t) {
  t = clamp01(t);
  return t * t;
}
inline float ease_out_quad(float t) {
  t = clamp01(t);
  return 1.0f - (1.0f - t) * (1.0f - t);
}
// Overshoots its target by a little and comes back (c = 1.7 is the classic feel).
inline float ease_out_back(float t, float c = 1.70158f) {
  t = clamp01(t);
  const float v = t - 1.0f;
  return 1.0f + (c + 1.0f) * v * v * v + c * v * v;
}
// Rounds to the nearest pixel.
inline int px(float v) { return static_cast<int>(std::lround(v)); }

// --- colour -----------------------------------------------------------------------

inline uint8_t byte(float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); }
inline Rgb mix(Rgb a, Rgb b, float t) {
  t = clamp01(t);
  return {static_cast<uint8_t>(std::lround(a.r + (b.r - a.r) * t)),
          static_cast<uint8_t>(std::lround(a.g + (b.g - a.g) * t)),
          static_cast<uint8_t>(std::lround(a.b + (b.b - a.b) * t))};
}
inline Rgb scale(Rgb c, float k) {
  k = clamp01(k);
  return {static_cast<uint8_t>(std::lround(c.r * k)), static_cast<uint8_t>(std::lround(c.g * k)),
          static_cast<uint8_t>(std::lround(c.b * k))};
}
inline float luma(Rgb c) { return (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255.0f; }
// White on a dark background, black on a light one: the colour of a bar or a cursor.
inline Rgb contrast(Rgb bg) { return luma(bg) > 0.5f ? gfx::kBlack : gfx::kWhite; }
// The colour with its hue turned by `turns` (1 = a full circle), saturation and value kept.
Rgb hue_rotate(Rgb c, float turns);

}  // namespace p64::intro

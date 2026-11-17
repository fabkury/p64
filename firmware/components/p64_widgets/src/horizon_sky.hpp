// The sky the horizon_rd and aquarium faces draw from (face_horizon_rd.cpp,
// face_aquarium.cpp): its colours by the sun's elevation, where a body sits across the
// frame, the constants of its rising and setting. The same numbers as
// tools/mock_clock_faces.py. Written for the horizon face, retired on 2026-10-21 (p104).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "p64/gfx/frame.hpp"
#include "sprite.hpp"

namespace p64::widgets::themed::horizon_sky {

using gfx::Rgb;

struct Stop {
  float elevation;
  Rgb top, near, away;  // the top of the sky, the horizon near the sun, the horizon away from it
};
constexpr Stop kSky[] = {
    {-18, {2, 4, 18}, {8, 12, 38}, {8, 12, 38}},        {-12, {5, 6, 30}, {28, 18, 60}, {12, 14, 46}},
    {-6, {16, 16, 64}, {140, 60, 90}, {40, 30, 80}},    {-2, {30, 34, 100}, {245, 118, 60}, {90, 60, 110}},
    {2, {48, 74, 160}, {255, 168, 84}, {150, 120, 150}}, {8, {44, 100, 205}, {240, 205, 150}, {190, 190, 210}},
    {16, {38, 104, 226}, {165, 205, 245}, {165, 205, 245}}, {35, {30, 96, 224}, {150, 205, 250}, {150, 205, 250}},
    {90, {24, 86, 220}, {150, 205, 250}, {150, 205, 250}},
};

inline Rgb lerp(Rgb a, Rgb b, float t) {
  const auto ch = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
  return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

inline void sky_palette(float el, Rgb &top, Rgb &near, Rgb &away) {
  constexpr int n = sizeof(kSky) / sizeof(kSky[0]);
  if (el <= kSky[0].elevation) {
    top = kSky[0].top, near = kSky[0].near, away = kSky[0].away;
    return;
  }
  for (int i = 0; i + 1 < n; ++i) {
    const Stop &a = kSky[i], &b = kSky[i + 1];
    if (el <= b.elevation) {
      const float t = (el - a.elevation) / (b.elevation - a.elevation);
      top = lerp(a.top, b.top, t), near = lerp(a.near, b.near, t), away = lerp(a.away, b.away, t);
      return;
    }
  }
  top = kSky[n - 1].top, near = kSky[n - 1].near, away = kSky[n - 1].away;
}

// The viewer faces the equator: the sun rises on the left and sets on the right in the
// northern hemisphere (mirrored in the southern); the east-west component projected on the
// view plane, so a body near the zenith stays in the middle.
inline int sky_x(float az, float el, float lat) {
  float e = std::sin(az * 3.14159265f / 180) * std::cos(el * 3.14159265f / 180);
  if (lat < 0) e = -e;
  return static_cast<int>(std::lround(31.5f - 25.5f * e));
}

// The setting: at +0.7 degrees a disc rests on the line, by -0.83 (the almanac's sunset:
// the upper limb at the horizon, refraction included) it has slid behind it; the local
// line's pull on a body fades out over the first 5 degrees.
constexpr double kRests = 0.7, kSet = -0.83, kBlend = 5.0;

// The daylight, 0 at 6 degrees below the horizon to 1 at 8 above.
inline float daylight(float el) { return clamp01((el + 6) / 14); }

// A colour scaled per channel.
inline Rgb mul(Rgb c, const float k[3]) {
  const auto ch = [](uint8_t v, float f) { return static_cast<uint8_t>(std::min(255L, std::lround(v * f))); };
  return {ch(c.r, k[0]), ch(c.g, k[1]), ch(c.b, k[2])};
}

// A small 32-bit hash of a pixel and a tick: the water's glints.
inline uint32_t hash3(int x, int y, int t) {
  uint32_t v = (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u) ^ (static_cast<uint32_t>(t) * 83492791u);
  v = (v ^ (v >> 13)) * 1274126177u;
  return (v ^ (v >> 16)) & 0xFFFFu;
}

// The painted moon with its terminator: the lit side keeps the picture's craters, the dark
// side shows faintly on a dark sky.
inline void draw_moon(gfx::Frame &frame, const sprite::View &moon, int x, int y, float phase, float lat, bool dark_sky) {
  constexpr float kLit[3] = {1.25f, 1.25f, 1.3f};
  const float r = moon.w / 2.0f;
  const float c = std::cos(2 * 3.14159265f * phase);
  for (int yy = 0; yy < moon.h; ++yy) {
    const float v = (yy + 0.5f - r) / r;
    const float s = std::sqrt(std::max(0.0f, 1 - v * v));
    for (int xx = 0; xx < moon.w; ++xx) {
      if (!moon.inked(xx, yy)) continue;
      const float u = (xx + 0.5f - r) / r;
      const float uu = lat >= 0 ? u : -u;
      const bool lit = phase < 0.5f ? uu > s * c : uu < -s * c;
      const int X = x - moon.w / 2 + xx, Y = y - moon.h / 2 + yy;
      if (lit) {
        frame.set(X, Y, mul(moon.colour(xx, yy), kLit));
      } else if (dark_sky) {
        frame.set(X, Y, lerp(frame.get(X, Y), {60, 62, 90}, 0.5f));
      }
    }
  }
}

// A sprite's inked pixels, each scaled by `k`.
inline void blit_scaled(gfx::Frame &frame, const sprite::View &v, int x, int y, const float k[3]) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) frame.set(x + xx, y + yy, mul(v.colour(xx, yy), k));
}

}  // namespace p64::widgets::themed::horizon_sky

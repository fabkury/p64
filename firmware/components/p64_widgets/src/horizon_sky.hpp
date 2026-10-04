// What the two horizon faces share (face_horizon.cpp and face_horizon_rd.cpp): the sky's
// colours by the sun's elevation, where a body sits across the frame, the constants of
// its rising and setting. The same numbers as tools/mock_clock_faces.py.
#pragma once

#include <cmath>
#include <cstdint>

#include "p64/gfx/frame.hpp"

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

}  // namespace p64::widgets::themed::horizon_sky

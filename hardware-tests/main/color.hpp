// p64 -- small colour helpers shared by the scenes.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "display.hpp"

namespace p64 {

inline uint8_t to_byte(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// h in turns (any value; wraps), s and v in [0, 1].
inline Rgb hsv_to_rgb(float h, float s, float v) {
  const float hh = (h - std::floor(h)) * 6.0f;
  const int sector = static_cast<int>(hh) % 6;
  const float f = hh - std::floor(hh);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
  float r, g, b;
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return Rgb{to_byte(r), to_byte(g), to_byte(b)};
}

}  // namespace p64

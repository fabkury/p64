// p64 -- alpha compositing helpers shared by the decoders: flatten RGBA onto RGB888
// over the background colour, in gamma space like a browser (spec 3.3).
#pragma once

#include <cstddef>
#include <cstdint>

#include "p64/gfx/frame.hpp"

namespace p64::decode {

// Exact (x / 255) for 0..65535.
inline uint8_t div255(uint32_t x) {
  x += 128u;
  return static_cast<uint8_t>((x + (x >> 8)) >> 8);
}

// Straight (non-premultiplied) alpha over the background.
inline void flatten_rgba(const uint8_t *src, uint8_t *dst, size_t pixels, gfx::Rgb bg) {
  for (size_t i = 0; i < pixels; ++i, src += 4, dst += 3) {
    const uint32_t a = src[3];
    if (a == 255u) {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    } else if (a == 0u) {
      dst[0] = bg.r;
      dst[1] = bg.g;
      dst[2] = bg.b;
    } else {
      const uint32_t inv = 255u - a;
      dst[0] = div255(src[0] * a + bg.r * inv);
      dst[1] = div255(src[1] * a + bg.g * inv);
      dst[2] = div255(src[2] * a + bg.b * inv);
    }
  }
}

// Premultiplied alpha (libwebp's MODE_rgbA) over the background.
inline void flatten_premultiplied(const uint8_t *src, uint8_t *dst, size_t pixels, gfx::Rgb bg) {
  for (size_t i = 0; i < pixels; ++i, src += 4, dst += 3) {
    const uint32_t a = src[3];
    if (a == 255u) {
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
    } else if (a == 0u) {
      dst[0] = bg.r;
      dst[1] = bg.g;
      dst[2] = bg.b;
    } else {
      const uint32_t inv = 255u - a;
      dst[0] = div255(src[0] * 255u + bg.r * inv);
      dst[1] = div255(src[1] * 255u + bg.g * inv);
      dst[2] = div255(src[2] * 255u + bg.b * inv);
    }
  }
}

}  // namespace p64::decode

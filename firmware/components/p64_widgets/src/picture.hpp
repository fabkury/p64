// Pictures a clock face keeps as PNG files in the firmware (clock_assets' `Png`, from
// assets/clock-png) and decodes into RAM when it starts: the Retro Diffusion art of the
// horizon_rd and aquarium faces (p076), 27 KB of flash where the pixels would be 600 KB.
// The decoded pixels are RGBA with a 0/255 alpha, read through sprite::View like a baked
// sprite. Pure (host-tested): the decoding is p64_decode's PNG decoder; on the device its
// buffers and these, all above malloc's internal-RAM threshold, land in PSRAM.
#pragma once

#include <cstdint>
#include <vector>

#include "clock_assets.hpp"
#include "sprite.hpp"

namespace p64::widgets::picture {

struct Picture {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;  // w*h*4, row-major
  bool ok() const { return w > 0 && h > 0; }
  sprite::View view() const { return {rgba.data(), w, w, h}; }
  // The `i`-th of `count` equal cells laid side by side, no gap (an animation's frames).
  sprite::View frame(int count, int i) const { return {rgba.data() + i * (w / count) * 4, w, w / count, h}; }
};

// Decodes a static PNG; a pixel is opaque (alpha 255) where its alpha is 128 or more.
// False (and an empty picture) when the file does not decode.
bool decode(const assets::Png &png, Picture &out);

}  // namespace p64::widgets::picture

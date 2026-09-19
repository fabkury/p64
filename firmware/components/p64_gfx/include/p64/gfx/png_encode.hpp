// p64 -- a minimal PNG encoder for the live preview and screenshots: RGB888 to a PNG
// with stored (uncompressed) deflate blocks. A 64x64 frame becomes about 12.4 KB; no
// zlib needed, no ESP-IDF includes.
#pragma once

#include <cstdint>
#include <vector>

namespace p64::gfx {

std::vector<uint8_t> encode_png_rgb(const uint8_t *rgb, int width, int height);

}  // namespace p64::gfx

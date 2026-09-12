// p64 -- table of the GIF files embedded in the firmware.
//
// The table itself is generated at configure time by main/CMakeLists.txt from
// assets/gifs/*.gif (see gif_assets.cpp.in); the bytes live memory-mapped in flash.
#pragma once

#include <cstddef>
#include <cstdint>

namespace p64 {

struct GifAsset {
  const char *name;     // file name, e.g. "9cA9_64x64_teatime.gif"
  const uint8_t *data;  // start of the file in flash
  size_t size;          // bytes
};

extern const GifAsset kGifAssets[];
extern const size_t kGifAssetCount;

}  // namespace p64

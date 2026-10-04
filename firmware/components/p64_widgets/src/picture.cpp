#include "picture.hpp"

#include <algorithm>

#include "p64/decode/png_decoder.hpp"

namespace p64::widgets::picture {

// The decoder flattens transparency over a background colour and keeps no alpha, so the
// picture is flattened twice, over black and over white: a pixel that comes out the same
// both times is opaque, one that follows the background is transparent, and one in
// between (soft alpha, which these assets do not have) is opaque from half up.
bool decode(const assets::Png &png, Picture &out) {
  out = Picture{};
  decode::PngDecoder dec;
  uint32_t delay = 0;
  if (!dec.open(png.data, png.size, gfx::kBlack) || dec.info().animated || !dec.next(delay)) return false;
  const int w = dec.info().width, h = dec.info().height;
  const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
  std::vector<uint8_t> rgba(pixels * 4);
  const uint8_t *src = dec.canvas();
  for (size_t i = 0; i < pixels; ++i) {
    rgba[i * 4] = src[i * 3];
    rgba[i * 4 + 1] = src[i * 3 + 1];
    rgba[i * 4 + 2] = src[i * 3 + 2];
  }
  dec.set_background({255, 255, 255});
  if (!dec.next(delay)) return false;
  src = dec.canvas();
  for (size_t i = 0; i < pixels; ++i) {
    // over white minus over black is 255 - alpha in every channel
    const int through = src[i * 3] - rgba[i * 4];
    rgba[i * 4 + 3] = through < 128 ? 255 : 0;
    if (through > 0 && through < 128) {  // soft alpha: undo the blend over black
      for (int k = 0; k < 3; ++k) rgba[i * 4 + k] = static_cast<uint8_t>(std::min(255, rgba[i * 4 + k] * 255 / (255 - through)));
    }
  }
  out.w = w;
  out.h = h;
  out.rgba = std::move(rgba);
  return true;
}

}  // namespace p64::widgets::picture

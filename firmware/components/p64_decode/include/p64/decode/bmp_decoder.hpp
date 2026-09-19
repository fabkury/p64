// p64 -- BMP decoder, hand-written: 1/4/8-bit palette, 16/24/32-bit truecolor, RLE4 and
// RLE8, BITFIELDS masks, bottom-up and top-down rows, the 12-byte core header and the
// 40 to 124-byte info headers. Alpha is honoured only with an explicit alpha mask
// (classic 32-bit BI_RGB files render opaque: their fourth byte is garbage in the wild).
// Ported from p3a's bmp_animation_decoder.c. Always a static image.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "p64/decode/decoder.hpp"

namespace p64::decode {

class BmpDecoder final : public Decoder {
 public:
  bool open(const uint8_t *data, size_t size, gfx::Rgb background) override;
  const Info &info() const override { return info_; }
  bool next(uint32_t &delay_ms) override;
  const uint8_t *canvas() const override { return canvas_rgb_.data(); }
  bool at_end() const override { return at_end_; }
  void rewind() override { at_end_ = false; }
  void set_background(gfx::Rgb background) override { background_ = background; }
  bool is_open() const override { return open_; }
  const char *error() const override { return error_; }

 private:
  Info info_;
  gfx::Rgb background_{};
  bool open_ = false;
  bool at_end_ = false;
  const char *error_ = "";
  std::vector<uint8_t> canvas_rgb_;
  std::vector<uint8_t> pixels_;  // RGB888 or RGBA8888 as decoded
  bool has_alpha_ = false;
};

}  // namespace p64::decode

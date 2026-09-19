// p64 -- WebP decoder on libwebp (components/libwebp): animated files through
// WebPAnimDecoder (premultiplied RGBA canvas, composited by the library), static files
// decoded once at open. Flattened over the background colour into the RGB888 canvas.
// Ported from p3a's webp_animation_decoder.c.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "p64/decode/decoder.hpp"

struct WebPAnimDecoder;

namespace p64::decode {

class WebPDecoder final : public Decoder {
 public:
  WebPDecoder() = default;
  ~WebPDecoder() override;
  WebPDecoder(const WebPDecoder &) = delete;
  WebPDecoder &operator=(const WebPDecoder &) = delete;

  bool open(const uint8_t *data, size_t size, gfx::Rgb background) override;
  const Info &info() const override { return info_; }
  bool next(uint32_t &delay_ms) override;
  const uint8_t *canvas() const override { return canvas_rgb_.data(); }
  bool at_end() const override { return at_end_; }
  void rewind() override;
  void set_background(gfx::Rgb background) override { background_ = background; }
  bool is_open() const override { return open_; }
  const char *error() const override { return error_; }

 private:
  void close();
  void fail(const char *why);

  Info info_;
  gfx::Rgb background_{};
  bool open_ = false;
  bool at_end_ = false;
  const char *error_ = "";
  std::vector<uint8_t> canvas_rgb_;

  WebPAnimDecoder *anim_ = nullptr;
  uint32_t frames_emitted_ = 0;
  int last_timestamp_ms_ = 0;
  std::vector<uint8_t> static_pixels_;  // RGBA8888 (alpha) or RGB888 (opaque)
  bool static_has_alpha_ = false;
};

}  // namespace p64::decode

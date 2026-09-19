// p64 -- PNG and APNG decoder on libpng with the APNG read patch (components/libpng).
// Static PNGs are decoded once at open; APNGs keep the read structs alive and composite
// each fcTL sub-frame onto a persistent RGBA canvas (dispose and blend ops per the APNG
// spec), flattened over the background colour into the RGB888 canvas per frame.
// Ported from p3a's png_animation_decoder.c.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "p64/decode/decoder.hpp"

struct png_struct_def;
struct png_info_def;

namespace p64::decode {

class PngDecoder final : public Decoder {
 public:
  PngDecoder() = default;
  ~PngDecoder() override;
  PngDecoder(const PngDecoder &) = delete;
  PngDecoder &operator=(const PngDecoder &) = delete;

  bool open(const uint8_t *data, size_t size, gfx::Rgb background) override;
  const Info &info() const override { return info_; }
  bool next(uint32_t &delay_ms) override;
  const uint8_t *canvas() const override { return canvas_rgb_.data(); }
  bool at_end() const override { return at_end_; }
  void rewind() override;
  void set_background(gfx::Rgb background) override { background_ = background; }
  bool is_open() const override { return open_; }
  const char *error() const override { return error_; }

  // libpng's read callback needs these.
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;

 private:
  bool open_stream();       // APNG: fresh read structs positioned after the header
  void destroy_stream();
  bool apng_rewind();
  bool apng_decode_one();   // decodes and composites the next animation frame
  void apply_pending_dispose();
  bool decode_static();
  void fail(const char *why);
  void close();

  Info info_;
  gfx::Rgb background_{};
  bool open_ = false;
  bool at_end_ = false;
  const char *error_ = "";
  std::vector<uint8_t> canvas_rgb_;  // width*height*3, what canvas() returns

  // Static path
  std::vector<uint8_t> static_pixels_;  // RGB888 or RGBA8888 as decoded
  bool static_has_alpha_ = false;

  // APNG path
  bool animated_ = false;
  png_struct_def *png_ = nullptr;
  png_info_def *pinfo_ = nullptr;
  int interlace_passes_ = 1;
  uint32_t num_frames_ = 0;
  uint32_t frames_emitted_ = 0;
  uint32_t last_delay_ms_ = 0;
  bool first_frame_hidden_ = false;
  std::vector<uint8_t> canvas_rgba_;
  std::vector<uint8_t> subframe_;
  std::vector<uint8_t> prev_snapshot_;
  uint8_t pending_dispose_ = 0;
  bool have_pending_dispose_ = false;
  uint32_t pend_x_ = 0, pend_y_ = 0, pend_w_ = 0, pend_h_ = 0;
};

}  // namespace p64::decode

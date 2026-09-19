// p64 -- GIF decoder: bitbank2/AnimatedGIF in RAW mode with p64's own compositor
// (all four disposal methods, 1-bit transparency, canvas initialised to the background
// colour). Ported from the hardware tests' GifPlayer, which tools/gifcheck verified
// pixel-exact against Pillow.
#pragma once

#include <memory>

#include "AnimatedGIF.h"
#include "p64/decode/decoder.hpp"

namespace p64::decode {

class GifDecoder final : public Decoder {
 public:
  GifDecoder();
  ~GifDecoder() override;
  GifDecoder(const GifDecoder &) = delete;
  GifDecoder &operator=(const GifDecoder &) = delete;

  bool open(const uint8_t *data, size_t size, gfx::Rgb background) override;
  const Info &info() const override { return info_; }
  bool next(uint32_t &delay_ms) override;
  const uint8_t *canvas() const override { return canvas_.get(); }
  bool at_end() const override { return at_end_; }
  void rewind() override;
  void set_background(gfx::Rgb background) override { background_ = background; }
  bool is_open() const override { return open_; }
  const char *error() const override { return error_; }

  uint32_t frames_decoded() const { return frames_; }  // since open()
  uint32_t loops() const { return loops_; }            // wrap-arounds since open()

 private:
  struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t disposal = 0;
  };

  static void draw_callback(GIFDRAW *d);
  void on_line(const GIFDRAW *d);
  void begin_frame(const GIFDRAW *d);
  void dispose_previous();
  void fill_background(int x, int y, int w, int h);
  void close();
  void fail(const char *why);

  std::unique_ptr<AnimatedGIF> gif_;  // ~27 KB, so it lives on the heap
  std::unique_ptr<uint8_t[]> canvas_;
  std::unique_ptr<uint8_t[]> backup_;
  Info info_;
  gfx::Rgb background_{};
  bool open_ = false;
  bool frame_started_ = false;
  bool at_end_ = false;
  Rect current_, previous_;
  uint32_t frames_ = 0, loops_ = 0, frames_this_loop_ = 0;
  const char *error_ = "";
};

}  // namespace p64::decode

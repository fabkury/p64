// p64 -- frame-rate test: plays the embedded GIFs as fast as the panel accepts frames,
// ignoring their frame delays, 10 s per GIF in an order shuffled at boot, with the
// delivered frame rate drawn top-right over a black box.
#pragma once

#include <cstdint>
#include <vector>

#include "gif_player.hpp"
#include "scene.hpp"

namespace p64 {

class GifFpsScene : public Scene {
 public:
  const char *name() const override { return "gif playback, max speed"; }
  uint32_t duration_ms() const override { return kRunForever; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, const FrameInfo &info) override;

 private:
  bool start_gif(size_t order_index, uint32_t now_ms);
  void advance(uint32_t now_ms);
  void log_gif_stats(uint32_t now_ms) const;
  void draw_counter(Frame &frame, float fps) const;

  std::vector<size_t> order_;
  size_t position_ = 0;
  uint32_t gif_start_ms_ = 0;
  uint32_t gif_frames_ = 0;
  uint64_t gif_decode_us_ = 0;
  GifPlayer player_;
  Scaler scaler_;
};

}  // namespace p64

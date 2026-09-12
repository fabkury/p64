// p64 -- the GIF show: plays the embedded GIFs at their intended speed, one after the
// other in an order shuffled at boot, with the wall clock top-left. Menuconfig can
// turn it into the frame-rate test (ignore delays, show fps top-right).
#pragma once

#include <cstdint>
#include <vector>

#include "gif_player.hpp"
#include "scene.hpp"

namespace p64 {

class GifShowScene : public Scene {
 public:
  const char *name() const override;
  uint32_t duration_ms() const override { return kRunForever; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, const FrameInfo &info) override;

 private:
  bool start_gif(size_t order_index, uint32_t now_ms);
  void advance(uint32_t now_ms);
  void log_gif_stats(uint32_t now_ms) const;
  bool decode_next(uint32_t now_ms);
  void draw_overlays(Frame &frame, float fps) const;

  std::vector<size_t> order_;
  size_t position_ = 0;
  uint32_t gif_start_ms_ = 0;
  uint32_t gif_frames_ = 0;
  uint64_t gif_decode_us_ = 0;
  uint32_t next_frame_ms_ = 0;  // when the next GIF frame is due
  bool single_frame_ = false;   // still image: decode once, then hold
  char clock_text_[8] = "--:--";
  GifPlayer player_;
  Scaler scaler_;
};

}  // namespace p64

// p64 -- phase 1: a ball under gravity, bouncing on the panel's native bottom edge,
// cycling once through the fully saturated hue spectrum during the phase, with the
// delivered frame rate in the top-right corner.
//
// What you see tells you the panel's native orientation: the floor line is row
// kHeight-1 ("down"), and the small L-shaped marker in the corner is the origin
// (white pixel = (0,0), red arm = +x, green arm = +y).
#pragma once

#include "scene.hpp"

namespace p64 {

class BallScene : public Scene {
 public:
  const char *name() const override { return "bouncing ball"; }
  uint32_t duration_ms() const override { return 10 * 1000; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, const FrameInfo &info) override;

 private:
  void step(float dt_s);
  void draw(Frame &frame, float hue_turns, float fps) const;

  float x_ = 0, y_ = 0;    // centre, pixels
  float vx_ = 0, vy_ = 0;  // pixels per second
};

}  // namespace p64

// p64 -- phases 2 and 3: a full-white panel fading from maximum brightness to
// fully off over 20 s and back up over another 20 s.
//
// The Linear curve steps the driver's 0-255 brightness evenly (an honest look at
// the hardware: where it visibly cuts off near zero, how coarse the low end is).
// The Perceptual curve steps CIE 1931 lightness evenly instead, so the change
// looks even to the eye and most of the time is spent in the dim region.
#pragma once

#include "scene.hpp"

namespace p64 {

class FadeScene : public Scene {
 public:
  enum class Curve { Linear, Perceptual };

  explicit FadeScene(Curve curve) : curve_(curve) {}

  const char *name() const override {
    return curve_ == Curve::Linear ? "white fade, linear in driver units" : "white fade, perceptually linear";
  }
  uint32_t duration_ms() const override { return 40 * 1000; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, uint32_t t_ms, float dt_s) override;

 private:
  Curve curve_;
  int last_ = -1;
};

}  // namespace p64

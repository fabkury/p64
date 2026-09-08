// p64 -- phase 4: a hue wheel stretched to fill the square panel.
//
// Hue is the angle around the centre. Saturation grows with the square
// (Chebyshev) distance from the centre, so the rings of equal saturation are
// concentric squares: pure hues along the edges, white in the middle. The whole
// wheel makes one full turn during the phase.
#pragma once

#include "scene.hpp"

namespace p64 {

class HueWheelScene : public Scene {
 public:
  const char *name() const override { return "square hue wheel"; }
  uint32_t duration_ms() const override { return 20 * 1000; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, uint32_t t_ms, float dt_s) override;

 private:
  void build_tables();

  bool tables_ready_ = false;
  uint8_t hue_[kWidth * kHeight] = {};         // 0..255 = one turn
  uint8_t saturation_[kWidth * kHeight] = {};  // 0 at the centre, 255 at the edge
};

}  // namespace p64

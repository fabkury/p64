// p64 -- phase 2: full power. Every pixel pure white at the maximum brightness
// allowed by P64_MAX_BRIGHTNESS, held for the whole phase.
#pragma once

#include "scene.hpp"

namespace p64 {

class WhiteScene : public Scene {
 public:
  const char *name() const override { return "full power, all white"; }
  uint32_t duration_ms() const override { return 20 * 1000; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, const FrameInfo &info) override;
};

}  // namespace p64

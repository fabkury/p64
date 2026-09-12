// p64 -- a Scene is one phase of the test sequence.
#pragma once

#include <cstdint>

#include "display.hpp"
#include "sdkconfig.h"

namespace p64 {

struct FrameInfo {
  uint32_t t_ms;  // time since the scene's enter()
  float dt_s;     // time since the previous render() call
  float fps;      // frames actually presented per second, measured over the last ~0.5 s
};

class Scene {
 public:
  virtual ~Scene() = default;
  virtual const char *name() const = 0;
  virtual uint32_t duration_ms() const = 0;
  // Called once when the scene starts. Must leave the panel showing its first frame.
  virtual void enter(Display &display, Frame &frame) = 0;
  // Called once per frame. Return true when `frame` changed and must be presented.
  virtual bool render(Display &display, Frame &frame, const FrameInfo &info) = 0;
};

// Brightness cap from menuconfig (P64_MAX_BRIGHTNESS), honoured by every scene.
inline uint8_t max_brightness() { return static_cast<uint8_t>(CONFIG_P64_MAX_BRIGHTNESS); }

}  // namespace p64

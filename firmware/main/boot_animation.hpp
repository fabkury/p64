// p64 -- the boot animation: a short procedural sequence shown from power-on until the
// first artwork is ready (spec section 15.1). Pure drawing into a Frame.
#pragma once

#include <cstdint>

#include "p64/gfx/frame.hpp"

namespace p64 {

class BootAnimation {
 public:
  // Draws the frame for time t_ms since the animation started. Returns false once
  // the animation has run its course (the caller may hold the last frame).
  bool render(gfx::Frame &frame, uint32_t t_ms, uint32_t duration_ms) const;
};

// A quiet placeholder for when nothing else is playing yet (M0 only): a slow, dim
// breathing gradient so a running device is visibly running.
class IdlePattern {
 public:
  void render(gfx::Frame &frame, uint32_t t_ms) const;
};

}  // namespace p64

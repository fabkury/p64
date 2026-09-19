// p64 -- FrameSource: anything the player can put on the panel, frame by frame: an
// artwork (file), a status screen, the boot animation, later widgets and streams. Every
// swap goes through the same seamless path (spec 3.6) because the player treats them
// all alike.
#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "p64/gfx/frame.hpp"

namespace p64::playback {

class FrameSource {
 public:
  virtual ~FrameSource() = default;
  virtual const std::string &name() const = 0;
  // Produces the frame to show next. `delay_ms` receives how long it stays (0 = until
  // replaced; the player applies the 60 fps cap). `due_us` is when the frame is expected
  // on the panel (0 = as soon as possible), so time-dependent sources such as a clock
  // draw the right instant even when the player works ahead. False = the source failed
  // for good; error() says why.
  virtual bool next_frame(gfx::Frame &out, uint32_t &delay_ms, int64_t due_us) = 0;
  // True once the source is known to have a single frame (the player stops asking).
  virtual bool is_static() const = 0;
  virtual const char *error() const { return ""; }
};

// One fixed frame: status screens, the dark panel of a pause. Holds a 12 KB copy, so
// allocate it in PSRAM (std::allocate_shared with a PSRAM allocator).
class StaticSource : public FrameSource {
 public:
  StaticSource(std::string name, const gfx::Frame &frame) : name_(std::move(name)) { frame_.copy_from(frame); }
  const std::string &name() const override { return name_; }
  bool next_frame(gfx::Frame &out, uint32_t &delay_ms, int64_t) override {
    out.copy_from(frame_);
    delay_ms = 0;
    return true;
  }
  bool is_static() const override { return true; }

 private:
  std::string name_;
  gfx::Frame frame_;
};

}  // namespace p64::playback

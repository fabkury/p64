// p64 -- what a rotary encoder's raw readings mean (pure, host-tested): the seesaw
// counts detents itself and hands over a 32-bit position, so a poll turns a position
// difference into "turned N detents" (wrap-safe, sign by the invert setting) and the
// switch level into press, release and long-press events with a two-sample debounce.
#pragma once

#include <cstdint>

namespace p64::inputs {

struct EncoderEvent {
  int32_t turned = 0;       // detents since the last poll, positive = clockwise (once invert is right)
  bool pressed = false;     // the switch went down at this poll
  bool released = false;    // the switch came up at this poll
  bool long_press = false;  // held for kLongPressMs (reported once per hold, before the release)
  bool any() const { return turned != 0 || pressed || released || long_press; }
};

class EncoderTracker {
 public:
  static constexpr uint32_t kLongPressMs = 700;

  void set_invert(bool invert) { invert_ = invert; }
  bool invert() const { return invert_; }
  // One poll: `position` is the seesaw's counter, `down` the raw switch level (true =
  // pressed), `t_ms` monotonic. The first call only records the position.
  EncoderEvent feed(uint32_t t_ms, int32_t position, bool down);
  // Forget the position (after a board reset or re-open) without losing the counters.
  void resync();

  int32_t position() const { return last_position_; }
  bool held() const { return held_; }
  int64_t detents() const { return detents_; }      // net detents, sign applied
  uint32_t presses() const { return presses_; }
  uint32_t long_presses() const { return long_presses_; }

 private:
  bool invert_ = false;
  bool have_position_ = false;
  int32_t last_position_ = 0;
  int64_t detents_ = 0;
  bool raw_down_ = false;      // the previous raw sample
  bool held_ = false;          // the debounced switch state
  uint32_t down_since_ms_ = 0;
  bool long_reported_ = false;
  uint32_t presses_ = 0, long_presses_ = 0;
};

}  // namespace p64::inputs

#include "encoder_model.hpp"

namespace p64::inputs {

EncoderEvent EncoderTracker::feed(uint32_t t_ms, int32_t position, bool down) {
  EncoderEvent ev;
  if (have_position_) {
    // Two's-complement subtraction wraps correctly across INT32_MAX / INT32_MIN.
    const auto delta = static_cast<int32_t>(static_cast<uint32_t>(position) - static_cast<uint32_t>(last_position_));
    ev.turned = invert_ ? -delta : delta;
    detents_ += ev.turned;
  }
  have_position_ = true;
  last_position_ = position;

  // The switch: a level must be seen on two consecutive polls to count (20 ms at 50 Hz,
  // longer than the contact bounce of the encoder's push switch).
  if (down == raw_down_ && down != held_) {
    held_ = down;
    if (held_) {
      ev.pressed = true;
      ++presses_;
      down_since_ms_ = t_ms;
      long_reported_ = false;
    } else {
      ev.released = true;
    }
  }
  raw_down_ = down;
  if (held_ && !long_reported_ && t_ms - down_since_ms_ >= kLongPressMs) {
    long_reported_ = true;
    ev.long_press = true;
    ++long_presses_;
  }
  return ev;
}

void EncoderTracker::resync() { have_position_ = false; }

}  // namespace p64::inputs

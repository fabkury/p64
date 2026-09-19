// p64 -- the board's BOOT push button (GPIO0, active low), polled and debounced.
#pragma once

#include <cstdint>

namespace p64 {

class Button {
 public:
  void begin();
  // Call once per frame. Returns true exactly once per press.
  bool poll();

 private:
  bool stable_ = false;
  bool raw_ = false;
  int64_t raw_since_us_ = 0;
};

}  // namespace p64

// p64 -- the effective brightness (spec 3.2): the user's brightness, or the night
// schedule's target inside its window, capped by the ceiling; 0 means panel off. Pure
// (host-tested); the caller supplies the local time.
#pragma once

#include <cstdint>

#include "p64/system/settings.hpp"

namespace p64::system::night {

// True when `now_minutes` (minutes after local midnight) lies in [start, end); a window
// with end <= start crosses midnight.
bool in_window(uint16_t start_minutes, uint16_t end_minutes, uint16_t now_minutes);

// `local_minutes` is -1 while the time is unknown (the schedule then does not apply).
// `night_active` reports whether the window applied.
uint8_t effective_brightness(const Settings &s, int local_minutes, bool &night_active);

}  // namespace p64::system::night

// p64 -- the p64b rotary encoders (two Adafruit 5880 seesaw boards on the external I2C
// bus): the poll task, the boot identify, the counters, and the roles handed to the
// hooks (knob A brightness and pause, knob B next / previous and like; knob_rules.hpp,
// docs/hardware/encoders-soldered.md section 5).
#pragma once

#include "cJSON.h"
#include "p64/inputs/inputs.hpp"

namespace p64::inputs::encoders {

// Starts the poll task when CONFIG_P64_ENCODERS is on. Boards that do not answer are
// polled again every 5 s, so a knob plugged in later still appears.
void start(const Hooks &hooks);
// The settings that shape the events: act at all, exchange the roles, count the other way.
void apply(bool enabled, bool swap, bool invert);
// How many boards answer right now (0 to 2).
int present();
// Positions, switch states, counters and read errors per board, for /diag/encoders.
// With `scan`, also every 7-bit address on the external bus that acknowledges (about
// 15 ms; the poller's transactions interleave, the driver serialises the bus).
cJSON *json(bool scan = false);

}  // namespace p64::inputs::encoders

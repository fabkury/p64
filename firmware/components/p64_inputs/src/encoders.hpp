// p64 -- the p64b rotary encoders (two Adafruit 5880 seesaw boards on the external I2C
// bus): the poll task, the boot identify, the counters. Stage B, the probe: every detent
// and press is logged and counted; the roles (brightness, next / previous, pause, like)
// are stage C (docs/hardware/encoders-soldered.md, section 5).
#pragma once

#include "cJSON.h"

namespace p64::inputs::encoders {

// Starts the poll task when CONFIG_P64_ENCODERS is on. Boards that do not answer are
// polled again every 5 s, so a knob plugged in later still appears.
void start();
// How many boards answer right now (0 to 2).
int present();
// Positions, switch states, counters and read errors per board, for /diag/encoders.
cJSON *json();

}  // namespace p64::inputs::encoders

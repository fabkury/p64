// p64 -- inputs (spec 9): the IMU's tap gestures (single = next, double = previous),
// the gravity-based auto-rotation (spec 3.4) and the p64b rotary encoders (the probe
// for now). The BOOT button's factory reset lives in main/ops (it runs before anything
// else).
#pragma once

#include <cstdint>
#include <functional>

#include "cJSON.h"

namespace p64::inputs {

struct Hooks {
  std::function<void()> next;
  std::function<void()> previous;
  // The auto-rotation resolved a new value (read it with auto_rotation()).
  std::function<void()> rotation_changed;
};

// Starts the encoder poller (when configured) and the IMU sampler task; false when no
// IMU answers (taps and auto-rotation off; the encoders run regardless).
bool start(const Hooks &hooks);
bool imu_present();
// The rotation auto mode resolved (valid only when resolved()).
bool auto_rotation_resolved();
uint16_t auto_rotation();
// "The panel is upright now, showing rotation `rotation`": stores the current gravity
// angle as that reference. False without an IMU or before the first sample.
bool calibrate_upright(uint16_t rotation);
bool calibrated();
// Live readings and counters for the diagnostics page.
cJSON *imu_json();

// The p64b rotary encoders (CONFIG_P64_ENCODERS): how many boards answer, and their
// positions, switch states and counters for /diag/encoders. Stage B (the probe): the
// events are logged and counted, not yet acted on.
int encoders_present();
cJSON *encoders_json(bool scan = false);

}  // namespace p64::inputs

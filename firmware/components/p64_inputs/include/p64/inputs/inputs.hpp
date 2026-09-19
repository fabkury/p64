// p64 -- inputs (spec 9): the IMU's tap gestures (single = next, double = previous) and
// the gravity-based auto-rotation (spec 3.4). The BOOT button's factory reset lives in
// main/ops (it runs before anything else). Rotary encoders plug in here later.
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

// Starts the IMU sampler task; false when no IMU answers (taps and auto-rotation off).
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

}  // namespace p64::inputs

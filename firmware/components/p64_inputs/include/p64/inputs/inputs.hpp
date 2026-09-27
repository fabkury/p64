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
  std::function<void()> next;      // a single tap, or the navigate knob clockwise
  std::function<void()> previous;  // a double tap, or the navigate knob counter-clockwise
  // The auto-rotation resolved a new value (read it with auto_rotation()).
  std::function<void()> rotation_changed;
  // The p64b knobs (knob_rules.hpp): the brightness knob turned `detents` (positive =
  // up), its press toggles pause; the navigate knob's press likes the artwork on the
  // panel. Called on the poll task, whose stack is in PSRAM: nothing here may touch the
  // flash directly (settings go through the guarded settings path).
  std::function<void(int32_t detents)> brightness_step;
  std::function<void()> toggle_pause;
  std::function<void()> like;
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
// positions, switch states and counters for /diag/encoders. Settings `encoders_enabled`,
// `encoders_swap` and `encoders_invert` (group inputs) decide what the events do.
int encoders_present();
cJSON *encoders_json(bool scan = false);

}  // namespace p64::inputs

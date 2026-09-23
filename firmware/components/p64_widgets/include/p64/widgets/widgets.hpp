// p64 -- widgets (spec section 7): the clock, the weather forecast and the board's
// temperature/humidity sensor, each a FrameSource the show can put on the panel (the
// Widget state, interludes), plus the clock overlay for the Animation show (6.1). The
// weather fetcher and the sensor sampler run on their own small core-0 tasks.
#pragma once

#include <cstdint>
#include <memory>

#include "cJSON.h"
#include "p64/gfx/frame.hpp"
#include "p64/playback/frame_source.hpp"
#include "p64/system/settings.hpp"

namespace p64::widgets {

// Starts the sensor sampler and the weather fetcher (idle until a location is set).
bool start();

// A fresh source for a widget (allocated in PSRAM). Each renders its first frame at once.
std::shared_ptr<playback::FrameSource> make(system::WidgetKind kind);
const char *widget_name(system::WidgetKind kind);

// The clock overlay (spec 6.1): `overlay_key()` changes whenever the drawing would (the
// minute, the settings); 0 means "nothing to draw". `draw_overlay()` paints HH:MM with an
// outline in the chosen corner. Both run on the player task, the key first: the key
// converts the time and, when it changed, draws the overlay once into a cached sprite;
// `draw_overlay()` only stamps that sprite (faces::OverlaySprite).
uint32_t overlay_key();
void draw_overlay(gfx::Frame &frame);

// The sensor's latest reading (with the calibration offsets applied).
struct Reading {
  bool valid = false;
  float temperature_c = 0;
  float humidity = 0;
  float trend_c_per_hour = 0;  // from the last hour of samples (0 until an hour has passed)
  int64_t sampled_us = 0;
};
Reading sensor();
// The weather as JSON for the status document (nullptr when never fetched).
cJSON *weather_json();
void weather_refresh_now();

}  // namespace p64::widgets

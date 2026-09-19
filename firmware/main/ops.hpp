// p64 -- operations (spec 15): the BOOT-hold factory reset at power-on, the factory reset
// itself, confirming a new OTA image once it has run, and the night schedule's
// brightness applier. Lives in main because it touches every component.
#pragma once

#include <cstdint>
#include <functional>

#include "p64/gfx/frame.hpp"
#include "p64/playback/player.hpp"
#include "p64/system/settings.hpp"

namespace p64::ops {

// Starts the timers: the night schedule check (calls `reapply_display` when the
// effective brightness changes) and the image confirmation 30 s after boot.
void start(std::function<void()> reapply_display);
// At boot, after the player runs: when BOOT is held, waits up to 10 s (the panel counts
// down the last 3 s) and performs the factory reset; returns when BOOT is released.
void check_boot_hold(playback::Player &player, gfx::Frame &scratch);
// Erases settings, state, Wi-Fi credentials, Makapix credentials and the PIN, then
// reboots (into setup mode, since no Wi-Fi is known). Does not return.
[[noreturn]] void factory_reset();
// The brightness the panel should show now (spec 3.2), and whether the night window
// applies.
uint8_t effective_brightness(const system::Settings &s);
bool night_active();

}  // namespace p64::ops

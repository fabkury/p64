// p64 -- the show: the main state machine of the Animation show (spec 4.5 to 4.7, 5.2
// to 5.5, 6.1, 6.4, 15.1). It owns the active playset and its channel runtimes (local
// folder indexes and Makapix cache indexes), the scheduler, the history, the auto-swap
// timer, pause, play-this and the status screens; it hands sources to the player and
// never touches the card itself (the loader does, on its own task). Every mutation runs
// on the main task through a command queue; the API reads snapshots.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cJSON.h"
#include "p64/content/playset.hpp"
#include "p64/playback/player.hpp"
#include "p64/playback/renderer.hpp"

namespace p64::show {

// Starts the loader, subscribes to events and puts the boot animation up. Call after
// player.start().
bool init(playback::Player &player, playback::Renderer &renderer, uint32_t boot_animation_ms);
// Restores the active playset (after the card mount attempt) and asks for its scan.
void restore();
// The show loop; never returns. Call last from app_main.
[[noreturn]] void run();

// Commands, from any task (queued to the main task).
void next();
void previous();
void go_to(size_t history_index);
void pause();
void resume();
void set_paused(bool paused);
void reset_timer();
void refresh();
// Play this file now (play-this, spec 5.5). Checked here for existence and extension;
// the load happens on the loader and a failure is reported in status.
bool play_file(const std::string &absolute_path, std::string &error);
// Play a downloaded artwork now: a card path or "mem:<key>", with its Makapix post id
// (-1 for a plain URL) and a display name.
void play_downloaded(const std::string &path, int32_t post_id, const std::string &name);
// Activate a playset by name (built-in or stored); false with a reason when unknown.
bool activate_playset(const std::string &name, std::string &error);
// Activate a playset that is not stored (from the site, or the Followed built-in).
void activate_transient(const content::Playset &playset);
bool is_paused();
int32_t current_post_id();  // the Makapix post on the panel, -1 otherwise

// Snapshots for the API (any task). Each returns a new object the caller owns.
cJSON *status_json();    // the status document's "playback" object
cJSON *channels_json();  // the active playset's channels with their counts
cJSON *history_json();
cJSON *playsets_json();  // user playsets, built-ins with availability, the active name
std::string active_playset_name();

}  // namespace p64::show

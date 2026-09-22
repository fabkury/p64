// p64 -- the show's decision rules as pure code (no ESP-IDF include; host-tested in
// tests/host/unit/show.cpp). show.cpp owns the state and the effects and asks these for
// every decision that bugs have lived in: the swap timer's gate (the frozen artwork of
// 2026-09-21), the replacement of a pick prepared from a tiny cache (the one-file
// replay, M6), the stream takeover and what a stream keeps behind it (spec 8.3), the
// interlude roll (spec 6.1) and the status texts (spec 6.4). Review of 2026-09-22,
// proposal P-T1: the first slice of the show core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace p64::show::rules {

// --- channel status and the "no artwork" reason (spec 6.4) ---------------------------

struct ChannelFacts {
  bool supported = true;
  bool local = true;
  bool card_mounted = true;   // local channels
  bool needs_pairing = false;  // Makapix channels
  bool paired = false;
  bool online = true;
  size_t index_entries = 0;  // Makapix: entries in the index
  size_t cached = 0;         // Makapix: entries cached and within the size limit
  bool refreshed = false;    // Makapix: a listing has landed (last_refresh set)
  uint32_t oversized = 0;    // Makapix: listed at that refresh but over the size limit
  uint16_t max_side = 0;     // Makapix: the size limit, for the text
};
// Why a channel cannot supply artworks right now ("" when it can).
std::string channel_status(const ChannelFacts &f);

struct ChannelSummary {
  std::string status;  // channel_status()
  uint32_t available = 0;
};
// Why nothing can be shown: "" when some usable channel has entries; else the first
// channel's reason, "empty" when a usable channel simply has no files.
std::string no_artwork_reason(const std::vector<ChannelSummary> &channels);

// --- the auto-swap timer --------------------------------------------------------------

// The timer runs while an artwork is up, whatever the main state says (an artwork on
// the panel must never freeze), and while a widget is up inside the show (an
// interlude); the Widget state's own widget stays indefinitely (spec 6.2).
bool swap_timer_runs(bool paused, bool artwork_up, bool widget_up, bool show_active);
bool auto_swap_due(bool timer_runs, uint32_t interval_s, int64_t now_us, int64_t swapped_at_us);

// At an auto-swap each widget with an interlude probability is rolled in the fixed order
// clock, weather, temperature; the first that wins takes the slot. `roll` returns
// 0..99. Returns the index of the winner (0, 1, 2) or -1.
int roll_interlude(const uint8_t (&percent)[3], const std::function<uint32_t()> &roll);

// --- the prepared pick ------------------------------------------------------------------

// A Makapix pick prepared while its channel's cache was tiny is thrown away once the
// cache grows or when it would replay the artwork on the panel.
bool replace_prepared_pick(int32_t prepared_post, uint32_t pool_at_pick, uint32_t pool_now, bool artwork_up,
                           int32_t current_post);

// The entry a fresh pick should avoid in `channel`: the one on the panel, when it came
// from the same channel of the same playset (-1 = none).
int avoid_entry(bool have_current, int current_channel, const std::string &current_playset, int current_entry,
                int channel, const std::string &playset);

// --- streams (spec 8.3) ---------------------------------------------------------------

bool stream_allowed(bool stream_state, bool takeover_setting);

enum class StreamGate : uint8_t { No, Wait, Take };
// Whether an arriving stream takes the panel now, waits (the boot animation, or the
// pairing screen, finish first), or does not take it at all.
StreamGate stream_gate(bool frames_arriving, bool already_up, bool allowed, bool boot_running, bool pairing_screen);

// What is on the panel, and what the state put up while a stream holds it. Every source
// the show presents goes through present(); while a stream is up it is parked instead,
// so timers and history run on invisibly and the parked source returns when the stream
// ends. S is a shared pointer in the firmware, anything copyable in the tests.
template <typename S>
class Stage {
 public:
  // Returns true when `src` should go to the player now (false: parked behind a stream).
  bool present(S src) {
    if (stream_up_) {
      behind_ = std::move(src);
      return false;
    }
    on_panel_ = std::move(src);
    return true;
  }
  // The stream takes the panel; what was up waits behind it.
  void take(S stream) {
    behind_ = on_panel_;
    on_panel_ = std::move(stream);
    stream_up_ = true;
  }
  // The stream lets go: returns what should come back (empty when nothing was parked).
  S release() {
    stream_up_ = false;
    S back = std::move(behind_);
    behind_ = S{};
    return back;
  }
  bool stream_up() const { return stream_up_; }
  const S &on_panel() const { return on_panel_; }
  const S &behind() const { return behind_; }

 private:
  S on_panel_{};
  S behind_{};
  bool stream_up_ = false;
};

}  // namespace p64::show::rules

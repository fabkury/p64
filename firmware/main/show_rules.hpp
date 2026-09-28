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
  uint32_t oversized = 0;    // Makapix: listed at that refresh but outside the size limits
  uint16_t min_side = 0;     // Makapix: the size limits, for the text
  uint16_t max_side = 0;
  uint32_t unchecked = 0;    // providers: index entries a post-load file check has not reached
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

// --- interludes (spec 6.1, ADR 0014) ---------------------------------------------------
//
// The user enters, per widget, the median gap in minutes between interludes of that kind
// (0 = never, else 5..1440); the per-swap probability follows from the auto-swap
// interval. The gap in swaps is geometric, so the gap has median M when the chance of no
// win in M/T swaps is a half: p = 1 - 2^(-T / 60M). An interval longer than the gap is
// unsatisfiable (the gap can never be shorter than one interval): that kind is off. The
// kinds are rolled in priority order, the largest median first (ties: clock, weather,
// temperature), and the first to win takes the slot, which is "each rolled
// independently, the larger median wins a coincidence"; a lower kind loses the slots a
// higher one takes, so its rolled probability is p over the chance that no higher kind
// won, and its realised per-swap probability is exactly p again (capped at 1 when the
// higher kinds leave nothing). Checked offline by tools/interlude_sim.py first.
enum class InterludeState : uint8_t {
  Never = 0,           // the median is 0
  NoAutoSwap = 1,      // the auto-swap interval is 0: nothing swaps
  IntervalLonger = 2,  // the interval is longer than the median: off
  Rolled = 3,
};
struct InterludePlan {
  InterludeState state[3] = {InterludeState::Never, InterludeState::Never, InterludeState::Never};
  double per_swap[3] = {0, 0, 0};  // the realised per-swap probability p of each kind (0 unless Rolled)
  double rolled[3] = {0, 0, 0};    // what each kind is rolled with (>= per_swap: the compensation)
  uint8_t order[3] = {0, 1, 2};    // the roll order (indexes into the kinds), the first `count` used
  uint8_t count = 0;
};
InterludePlan interlude_plan(const uint16_t (&median_minutes)[3], uint32_t interval_s);
// Rolls the plan at an auto-swap; `roll` returns a uniform 32-bit number. Returns the
// index of the winner (0 clock, 1 weather, 2 temperature) or -1.
int roll_interlude(const InterludePlan &plan, const std::function<uint32_t()> &roll);

// A random clock face for a clock interlude (p057, 2026-09-28): uniform over the `count`
// faces except `previous` (255 = none yet), so the same face never comes twice running.
// `roll` is a uniform 32-bit number.
uint8_t random_face(uint8_t previous, uint32_t roll, uint8_t count);

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
// Whether an arriving stream takes the panel now, waits (the boot animation, or a screen
// that needs the user: pairing, setup, update, finish first), or does not take it at all.
StreamGate stream_gate(bool frames_arriving, bool already_up, bool allowed, bool boot_running, bool user_screen);

// The "connected" screen (spec 6.4, 15.1; decided 2026-09-26): a normal boot shows no
// network message. The decision waits for the boot animation to end; then the screen is
// skipped when something is up (an artwork, a widget, the pause frame, another screen,
// a stream) or on its way (an artwork prepared or loading), and shown otherwise. Before
// this the screen took the panel for its 15 s on every boot: the IP landed during the
// boot animation, nothing was "playing yet", and the first artwork waited until 18 s.
enum class ConnectedScreen : uint8_t { Wait, Skip, Show };
ConnectedScreen connected_screen(bool boot_running, bool something_up, bool something_coming);

// Setup mode on the panel (spec 6.4, 10.1; settled 2026-09-23): with no network saved the
// Setup screen holds the panel whatever plays; when a saved network is only down, artworks
// keep playing from the cache and the setup pages replace the "no artwork" screen only.
enum class SetupScreen : uint8_t { None, Holds, InsteadOfNoArtwork };
SetupScreen setup_screen(bool setup_mode, bool network_saved);

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

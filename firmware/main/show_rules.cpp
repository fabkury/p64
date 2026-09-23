#include "show_rules.hpp"

namespace p64::show::rules {

std::string channel_status(const ChannelFacts &f) {
  if (!f.supported) return "not supported yet";
  if (f.local) return f.card_mounted ? "" : "no card";
  if (f.needs_pairing && !f.paired) return "needs pairing";
  if (f.cached == 0) {
    if (f.index_entries > 0) return "downloading";
    if (!f.online) return "offline";
    if (!f.refreshed) return "no listing yet";
    // The listing landed empty: the channel has nothing, or nothing small enough.
    if (f.oversized > 0) {
      return "nothing fits " + std::to_string(f.max_side) + " px (" + std::to_string(f.oversized) + " too large)";
    }
    return "no artworks";
  }
  return "";
}

std::string no_artwork_reason(const std::vector<ChannelSummary> &channels) {
  std::string reason;
  for (const ChannelSummary &ch : channels) {
    if (ch.status.empty()) {
      if (ch.available > 0) return "";
      if (reason.empty()) reason = "empty";
    } else if (reason.empty()) {
      reason = ch.status;
    }
  }
  return reason.empty() ? "empty" : reason;
}

bool swap_timer_runs(bool paused, bool artwork_up, bool widget_up, bool show_active) {
  if (paused) return false;
  return artwork_up || (widget_up && show_active);
}

bool auto_swap_due(bool timer_runs, uint32_t interval_s, int64_t now_us, int64_t swapped_at_us) {
  return timer_runs && interval_s > 0 && now_us - swapped_at_us >= static_cast<int64_t>(interval_s) * 1000000;
}

int roll_interlude(const uint8_t (&percent)[3], const std::function<uint32_t()> &roll) {
  for (int i = 0; i < 3; ++i) {
    if (percent[i] == 0) continue;
    if (roll() % 100 < percent[i]) return i;
  }
  return -1;
}

bool replace_prepared_pick(int32_t prepared_post, uint32_t pool_at_pick, uint32_t pool_now, bool artwork_up,
                           int32_t current_post) {
  const bool repeat = artwork_up && current_post == prepared_post;
  return repeat || (pool_at_pick < 8 && pool_now > pool_at_pick);
}

int avoid_entry(bool have_current, int current_channel, const std::string &current_playset, int current_entry,
                int channel, const std::string &playset) {
  return (have_current && current_channel == channel && current_playset == playset) ? current_entry : -1;
}

bool stream_allowed(bool stream_state, bool takeover_setting) { return stream_state || takeover_setting; }

StreamGate stream_gate(bool frames_arriving, bool already_up, bool allowed, bool boot_running, bool user_screen) {
  if (!frames_arriving || already_up || !allowed) return StreamGate::No;
  if (boot_running || user_screen) return StreamGate::Wait;
  return StreamGate::Take;
}

SetupScreen setup_screen(bool setup_mode, bool network_saved) {
  if (!setup_mode) return SetupScreen::None;
  return network_saved ? SetupScreen::InsteadOfNoArtwork : SetupScreen::Holds;
}

}  // namespace p64::show::rules

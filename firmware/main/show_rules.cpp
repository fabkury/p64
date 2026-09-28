#include "show_rules.hpp"

#include <algorithm>
#include <cmath>

namespace p64::show::rules {

std::string channel_status(const ChannelFacts &f) {
  if (!f.supported) return "not supported yet";
  if (f.local) return f.card_mounted ? "" : "no card";
  if (f.needs_pairing && !f.paired) return "needs pairing";
  if (f.cached == 0) {
    // A provider that holds its entries back until their files are checked is checking,
    // not downloading (the Divoom channel of 2026-09-26 read "downloading" for half an
    // hour after a boot with every file already on the card).
    if (f.unchecked > 0) return "checking files";
    // An index read from the card before the network (2026-09-26) with nothing at hand
    // downloads only once online.
    if (!f.online) return "offline";
    if (f.index_entries > 0) return "downloading";
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

InterludePlan interlude_plan(const uint16_t (&median_minutes)[3], uint32_t interval_s) {
  InterludePlan plan;
  // Priority: the largest median first, ties in the fixed order clock, weather, temperature.
  for (uint8_t i = 0; i < 3; ++i) {
    if (median_minutes[i] == 0) continue;
    uint8_t at = plan.count;
    while (at > 0 && median_minutes[plan.order[at - 1]] < median_minutes[i]) {
      plan.order[at] = plan.order[at - 1];
      --at;
    }
    plan.order[at] = i;
    ++plan.count;
  }
  if (interval_s == 0) {
    for (uint8_t k = 0; k < plan.count; ++k) plan.state[plan.order[k]] = InterludeState::NoAutoSwap;
    plan.count = 0;
    return plan;
  }
  double surviving = 1.0;  // the chance that no higher-priority kind has won this swap
  uint8_t rolled = 0;
  for (uint8_t k = 0; k < plan.count; ++k) {
    const uint8_t i = plan.order[k];
    const double gap_s = 60.0 * median_minutes[i];
    if (static_cast<double>(interval_s) > gap_s) {
      plan.state[i] = InterludeState::IntervalLonger;
      continue;
    }
    const double p = 1.0 - std::exp2(-static_cast<double>(interval_s) / gap_s);
    plan.state[i] = InterludeState::Rolled;
    plan.per_swap[i] = p;
    plan.rolled[i] = surviving <= 0.0 ? 1.0 : std::min(1.0, p / surviving);
    surviving *= 1.0 - plan.rolled[i];
    plan.order[rolled++] = i;
  }
  plan.count = rolled;
  return plan;
}

int roll_interlude(const InterludePlan &plan, const std::function<uint32_t()> &roll) {
  for (uint8_t k = 0; k < plan.count; ++k) {
    const uint8_t i = plan.order[k];
    if (plan.rolled[i] >= 1.0) return i;
    // The threshold is exact to 2^-32; a probability that small is never asked for.
    const uint64_t threshold = static_cast<uint64_t>(plan.rolled[i] * 4294967296.0);
    if (static_cast<uint64_t>(roll()) < threshold) return i;
  }
  return -1;
}

uint8_t random_face(uint8_t previous, uint32_t roll, uint8_t count) {
  if (count <= 1) return 0;
  if (previous >= count) return static_cast<uint8_t>(roll % count);
  const uint8_t pick = static_cast<uint8_t>(roll % (count - 1));
  return pick >= previous ? static_cast<uint8_t>(pick + 1) : pick;
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

ConnectedScreen connected_screen(bool boot_running, bool something_up, bool something_coming) {
  if (boot_running) return ConnectedScreen::Wait;
  return (something_up || something_coming) ? ConnectedScreen::Skip : ConnectedScreen::Show;
}

SetupScreen setup_screen(bool setup_mode, bool network_saved) {
  if (!setup_mode) return SetupScreen::None;
  return network_saved ? SetupScreen::InsteadOfNoArtwork : SetupScreen::Holds;
}

}  // namespace p64::show::rules

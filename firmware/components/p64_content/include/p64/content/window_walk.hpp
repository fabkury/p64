// p64 -- how far a cardless provider walks a channel's listing (ADR 0018): only as far as
// the memory window needs. A channel starts kWindowWalkStart entries deep. A provider
// marks each entry it has put into the window in this lap with its own "seen" flag and
// passes over seen entries when it picks the next download; when no active channel has an
// unseen entry left, the lap ends here: a channel whose last walk stopped at its depth
// with more to list goes kWindowWalkStep deeper and is walked again (`cap` is the channel
// cache size, the limit with a card too), a channel at its end starts over with its
// "seen" flags cleared. Pure, shared by every provider's worker; host-tested through the
// Makapix policy (tests/host/unit/makapix.cpp).
#pragma once

#include <algorithm>
#include <cstdint>

namespace p64::content {

constexpr uint32_t kWindowWalkStart = 100;
constexpr uint32_t kWindowWalkStep = 100;

// Channels: a container of pointer-likes to channels with the fields active, loaded,
// walk_depth, walk_more and next_refresh_us; `entries_of(channel)` gives its entries,
// each with a `flags` byte. Returns whether anything changed (false: no active channel
// has an entry, nothing to do).
template <typename Channels, typename EntriesOf>
bool end_window_lap(const Channels &channels, uint32_t cap, uint8_t seen_flag, EntriesOf entries_of) {
  bool changed = false;
  for (const auto &ch : channels) {
    if (!ch->active || !ch->loaded) continue;
    auto &entries = entries_of(*ch);
    if (entries.empty()) continue;
    if (ch->walk_more && ch->walk_depth < cap) {
      ch->walk_depth = std::min(cap, ch->walk_depth + kWindowWalkStep);
      ch->next_refresh_us = 0;
    } else {
      for (auto &e : entries) e.flags &= static_cast<uint8_t>(~seen_flag);
    }
    changed = true;
  }
  return changed;
}

}  // namespace p64::content

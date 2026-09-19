// p64 -- Scheduler: which channel supplies the next artwork and which entry of it
// (spec 5.2, glossary "Channel selection", "Pick mode", "Channel offset"). The
// semantics are p3a's play scheduler: smooth weighted round-robin or stochastic
// selection over normalised weights with credit correction, and random or recency
// (cursor from the offset, wrapping) picks within a channel. The scheduler knows only
// counts and indices; the show maps them to files. Deterministic for a given seed, so it
// is host-tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p64::content {

enum class PickMode : uint8_t { Random = 0, Recency = 1 };
enum class ChannelSelect : uint8_t { Stochastic = 0, Swrr = 1 };

class Scheduler {
 public:
  static constexpr int32_t kWeightSum = 65536;

  struct Channel {
    uint32_t spec_weight = 0;  // from the playset
    uint32_t weight = 0;       // normalised share of kWeightSum among channels with entries
    int32_t credit = 0;        // round-robin credit (grows by weight per pick, drops by kWeightSum when chosen)
    uint32_t offset = 0;       // where the recency cursor starts
    uint32_t cursor = 0;       // next entry the recency pick returns
    bool cursor_started = false;
    uint32_t count = 0;        // entries pickable right now
    uint64_t rng = 0;          // PCG32 state for random picks
  };

  // Builds the channel table (all counts 0). The same seed reproduces the same picks.
  void configure(const std::vector<uint32_t> &spec_weights, const std::vector<uint32_t> &offsets, uint64_t seed);
  void set_channel_select(ChannelSelect mode) { select_ = mode; }
  void set_pick_mode(PickMode mode) { pick_ = mode; }
  ChannelSelect channel_select() const { return select_; }
  PickMode pick_mode() const { return pick_; }

  // How many entries channel i can pick from now; the weights are recalculated (a channel
  // without entries gets no share, spec 5.2).
  void set_count(size_t i, uint32_t count);
  // The channel of the next pick, by the selection mode; -1 when no channel has entries.
  int select_channel();
  // An entry index of channel i: random (avoiding `avoid` when there is a choice) or the
  // recency cursor (from the offset, wrapping past the end). -1 when the channel is empty.
  int pick_entry(size_t i, int32_t avoid);
  // Restarts the recency cursor of channel i from its offset.
  void reset_cursor(size_t i);

  size_t size() const { return channels_.size(); }
  const Channel &channel(size_t i) const { return channels_[i]; }
  uint32_t available_channels() const;

  // PCG32, the generator p3a uses.
  static uint32_t pcg32(uint64_t &state);

 private:
  void recalculate_weights();
  int select_swrr();
  int select_stochastic();

  std::vector<Channel> channels_;
  uint64_t rng_ = 0;
  ChannelSelect select_ = ChannelSelect::Stochastic;
  PickMode pick_ = PickMode::Random;
};

}  // namespace p64::content

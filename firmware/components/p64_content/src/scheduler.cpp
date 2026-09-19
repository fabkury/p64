#include "p64/content/scheduler.hpp"

#include <climits>

namespace p64::content {

uint32_t Scheduler::pcg32(uint64_t &state) {
  const uint64_t old = state;
  state = old * 6364136223846793005ULL + 1;
  const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
  const uint32_t rot = static_cast<uint32_t>(old >> 59u);
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void Scheduler::configure(const std::vector<uint32_t> &spec_weights, const std::vector<uint32_t> &offsets,
                          uint64_t seed) {
  channels_.clear();
  channels_.resize(spec_weights.size());
  rng_ = seed;
  pcg32(rng_);
  for (size_t i = 0; i < channels_.size(); ++i) {
    Channel &c = channels_[i];
    c.spec_weight = spec_weights[i];
    c.offset = i < offsets.size() ? offsets[i] : 0;
    c.rng = seed ^ (0x9E3779B97F4A7C15ULL * (i + 1));
    pcg32(c.rng);
  }
  recalculate_weights();
}

void Scheduler::set_count(size_t i, uint32_t count) {
  if (i >= channels_.size()) return;
  Channel &c = channels_[i];
  c.count = count;
  if (count == 0) {
    c.cursor_started = false;
  } else if (c.cursor >= count) {
    c.cursor = c.cursor % count;
  }
  recalculate_weights();
}

void Scheduler::recalculate_weights() {
  uint64_t total = 0;
  uint32_t available = 0;
  for (const Channel &c : channels_) {
    if (c.count > 0) {
      total += c.spec_weight;
      ++available;
    }
  }
  for (Channel &c : channels_) {
    if (c.count == 0) {
      c.weight = 0;
    } else if (total == 0) {
      c.weight = static_cast<uint32_t>(kWeightSum) / available;  // nobody set a weight: equal shares
    } else {
      c.weight = static_cast<uint32_t>(static_cast<uint64_t>(c.spec_weight) * kWeightSum / total);
    }
  }
}

uint32_t Scheduler::available_channels() const {
  uint32_t n = 0;
  for (const Channel &c : channels_) {
    if (c.count > 0 && c.weight > 0) ++n;
  }
  return n;
}

int Scheduler::select_channel() {
  if (available_channels() == 0) return -1;
  return select_ == ChannelSelect::Swrr ? select_swrr() : select_stochastic();
}

int Scheduler::select_swrr() {
  for (Channel &c : channels_) {
    if (c.weight > 0) c.credit += static_cast<int32_t>(c.weight);
  }
  int32_t best_credit = INT32_MIN;
  int candidates[64];
  int candidate_count = 0;
  for (size_t i = 0; i < channels_.size(); ++i) {
    const Channel &c = channels_[i];
    if (c.weight == 0) continue;
    if (c.credit > best_credit) {
      best_credit = c.credit;
      candidates[0] = static_cast<int>(i);
      candidate_count = 1;
    } else if (c.credit == best_credit && candidate_count < 64) {
      candidates[candidate_count++] = static_cast<int>(i);
    }
  }
  if (candidate_count == 0) return -1;
  const int best = candidate_count == 1 ? candidates[0] : candidates[pcg32(rng_) % candidate_count];
  channels_[best].credit -= kWeightSum;
  return best;
}

int Scheduler::select_stochastic() {
  // p3a: P(i) = w_i * clamp(1 + alpha * credit_i / Wsum, 0.1, 3.0), then the credits move
  // as in SWRR, so a channel that has been unlucky grows more likely.
  constexpr float kAlpha = 0.8f, kFloor = 0.1f, kCeil = 3.0f;
  float probs[64];
  float sum = 0;
  for (size_t i = 0; i < channels_.size(); ++i) {
    Channel &c = channels_[i];
    if (c.weight == 0) {
      probs[i] = 0;
      continue;
    }
    c.credit += static_cast<int32_t>(c.weight);
    float factor = 1.0f + kAlpha * static_cast<float>(c.credit) / static_cast<float>(kWeightSum);
    if (factor < kFloor) factor = kFloor;
    if (factor > kCeil) factor = kCeil;
    probs[i] = static_cast<float>(c.weight) * factor;
    sum += probs[i];
  }
  if (sum <= 0) return -1;
  const float r = static_cast<float>(pcg32(rng_)) / static_cast<float>(UINT32_MAX) * sum;
  float cumulative = 0;
  int best = -1;
  for (size_t i = 0; i < channels_.size(); ++i) {
    if (probs[i] <= 0) continue;
    cumulative += probs[i];
    if (r <= cumulative) {
      best = static_cast<int>(i);
      break;
    }
  }
  if (best < 0) {  // float rounding: the last channel with a share
    for (int i = static_cast<int>(channels_.size()) - 1; i >= 0; --i) {
      if (channels_[i].weight > 0) {
        best = i;
        break;
      }
    }
  }
  if (best >= 0) channels_[best].credit -= kWeightSum;
  return best;
}

int Scheduler::pick_entry(size_t i, int32_t avoid) {
  if (i >= channels_.size()) return -1;
  Channel &c = channels_[i];
  if (c.count == 0) return -1;
  if (pick_ == PickMode::Random) {
    int index = -1;
    for (int attempt = 0; attempt < 5; ++attempt) {
      index = static_cast<int>(pcg32(c.rng) % c.count);
      if (index != avoid || c.count == 1) break;  // no immediate repeat when there is a choice
    }
    return index;
  }
  if (!c.cursor_started) {
    c.cursor = c.offset % c.count;
    c.cursor_started = true;
  }
  if (c.cursor >= c.count) c.cursor = 0;
  int index = static_cast<int>(c.cursor);
  c.cursor = (c.cursor + 1) % c.count;
  if (index == avoid && c.count > 1) {  // the cursor came round to the current artwork
    index = static_cast<int>(c.cursor);
    c.cursor = (c.cursor + 1) % c.count;
  }
  return index;
}

void Scheduler::reset_cursor(size_t i) {
  if (i >= channels_.size()) return;
  channels_[i].cursor_started = false;
  channels_[i].cursor = 0;
}

}  // namespace p64::content

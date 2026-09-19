// p64 -- FrameQueue: the lock-free single-producer, single-consumer ring of ready frames
// between whoever produces pictures (the player, or the boot sequence) and the render
// task. Each slot carries its frame, the time it is due on the timeline, how long it must
// stay up, and the artwork generation it belongs to (a newer generation cuts older,
// unpresented slots: a swap is a hard cut, spec 4.5).
//
// The slots (12 KB frames) are provided by the owner, so the firmware can put them in
// PSRAM; the indices stay in this object, which must live in internal RAM (atomic
// read-modify-write is not available on PSRAM addresses).
#pragma once

#include <atomic>
#include <cstdint>

#include "p64/gfx/frame.hpp"

namespace p64::playback {

struct ReadySlot {
  gfx::Frame frame;
  int64_t due_us = 0;       // earliest presentation time (monotonic microseconds)
  uint32_t delay_us = 0;    // the frame stays up at least this long
  uint32_t generation = 0;  // artwork generation; 0 = boot/status content
  bool first = false;       // first frame of its generation
};

class FrameQueue {
 public:
  static constexpr unsigned kSlots = 3;

  // `slots` must point at kSlots constructed ReadySlots that outlive the queue.
  explicit FrameQueue(ReadySlot *slots) : slots_(slots) {}

  // Producer side: the slot to fill next, or nullptr while the ring is full.
  ReadySlot *producer_slot() {
    if (tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire) >= kSlots) return nullptr;
    return &slots_[tail_.load(std::memory_order_relaxed) % kSlots];
  }
  // Makes the slot from producer_slot() visible to the consumer.
  void producer_publish() {
    const ReadySlot &s = slots_[tail_.load(std::memory_order_relaxed) % kSlots];
    latest_generation_.store(s.generation, std::memory_order_relaxed);
    tail_.fetch_add(1, std::memory_order_release);
  }

  // Consumer side: the oldest published slot, or nullptr while the ring is empty.
  ReadySlot *consumer_peek() {
    if (head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire)) return nullptr;
    return &slots_[head_.load(std::memory_order_relaxed) % kSlots];
  }
  // Frees the slot returned by consumer_peek().
  void consumer_release() { head_.fetch_add(1, std::memory_order_release); }

  unsigned published() const {
    return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
  }
  // Generation of the most recently published slot (the consumer skips older ones).
  uint32_t latest_generation() const { return latest_generation_.load(std::memory_order_relaxed); }

 private:
  ReadySlot *slots_;
  std::atomic<unsigned> head_{0};  // next slot to consume
  std::atomic<unsigned> tail_{0};  // next slot to produce
  std::atomic<uint32_t> latest_generation_{0};
};

}  // namespace p64::playback

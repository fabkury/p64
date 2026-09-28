// p64 -- the TLS slot's queue as pure code (no ESP-IDF include; host-tested in
// tests/host/unit/net_slot.cpp). One transient TLS session at a time (ADR 0009): the
// slot is a recursive lock handed to its waiters in the order they asked, whatever their
// task priorities, so a low-priority worker (the Divoom provider at 3) is served after
// the Makapix fetcher (4) yields, not starved by it. fetch.cpp wraps it with a mutex and a
// condition variable and does the blocking.
//
// Why the order matters (2026-09-28): the FreeRTOS mutex it replaced woke the
// highest-priority waiter, and a holder that keeps a keep-alive session open across a
// whole page walk (up to 64 pages) re-took the slot at once, so the other provider waited
// for the whole walk and then for the next one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p64::net::tls_slot {

using Id = const void *;  // the task asking (a task handle); never dereferenced

struct Stats {
  uint32_t grants = 0;      // requests granted at once (free slot, or the holder again)
  uint32_t waits = 0;       // requests that had to queue
  uint32_t handoffs = 0;    // releases that woke the next in line
  uint32_t max_wait_ms = 0;  // the longest queue wait so far (the shell measures)
};

class Queue {
 public:
  // Asks for the slot. True: held now (a free slot, or the holder asking again, which
  // nests). False: queued behind the holder and the earlier askers; the shell waits until
  // holder() == id, which release() arranges.
  bool request(Id id);
  // Gives the slot up once (nested requests release in turn). When the last hold ends the
  // first in line becomes the holder (depth 1) and is returned so the shell wakes it;
  // nullptr when nothing changed hands. A release by a task that does not hold the slot
  // is ignored (returns nullptr).
  Id release(Id id);

  Id holder() const { return holder_; }
  uint32_t depth() const { return depth_; }
  size_t waiters() const { return queue_.size(); }
  // For a holder deciding whether to yield: someone else is waiting.
  bool contended() const { return holder_ != nullptr && !queue_.empty(); }
  const Stats &stats() const { return stats_; }
  void note_wait_ms(uint32_t ms) {
    if (ms > stats_.max_wait_ms) stats_.max_wait_ms = ms;
  }

 private:
  Id holder_ = nullptr;
  uint32_t depth_ = 0;
  std::vector<Id> queue_;  // first in line first
  Stats stats_;
};

}  // namespace p64::net::tls_slot

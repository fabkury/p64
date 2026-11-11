// p64 -- who frees a job that one task waits on and another finishes. Pure (no ESP-IDF
// include), host-tested.
//
// The waiter (an API handler, a knob press) queues a heap-allocated job and waits for the
// worker with a timeout; the worker may be busy for longer than that. Exactly one side
// must free the job, and the side that lost the race must never touch it again. Until
// 2026-11-11 the like job lived on the waiter's stack, and a worker that finished after
// the 20 s timeout wrote its result into a dead stack frame (finding F-10).
//
//   waiter:  if (waited in time)        -> read the result, free the job
//            else if (h.abandon())      -> return; the worker frees the job when it is done
//            else                       -> the worker finished meanwhile: wait for its
//                                          signal, read the result, free the job
//   worker:  write the result; if (h.finish()) signal the waiter (it owns the job)
//                              else             free the job (nobody is waiting any more)
#pragma once

#include <atomic>
#include <cstdint>

namespace p64::makapix {

class Handoff {
 public:
  // The waiter gave up. True: the job is the worker's now. False: the worker had already
  // finished; the job is still the waiter's and the signal is on its way.
  bool abandon() {
    uint8_t expected = kWaiting;
    return state_.compare_exchange_strong(expected, kAbandoned);
  }
  // The worker is done and the result is written. True: the waiter is waiting and owns
  // the job; signal it. False: the waiter gave up; the worker frees the job.
  bool finish() {
    uint8_t expected = kWaiting;
    return state_.compare_exchange_strong(expected, kFinished);
  }

 private:
  enum : uint8_t { kWaiting = 0, kAbandoned = 1, kFinished = 2 };
  std::atomic<uint8_t> state_{kWaiting};
};

}  // namespace p64::makapix

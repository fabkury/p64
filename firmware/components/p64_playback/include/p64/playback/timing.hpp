// p64 -- the two timing rules of playback, as pure code (host-tested in
// tests/host/unit/playback.cpp; review of 2026-09-22, proposal P-T1).
//
// Timeline (the player): when each produced frame is due. The first frame of a source
// is due as soon as it exists; every later frame is due at the previous due time plus
// the previous delay; a frame produced after its due time re-anchors the timeline at
// the moment it was produced (the no-drop rule, ADR 0003: slow motion, never a skip).
// Delays below the 60 fps presentation cap are raised to it (spec 4.3).
//
// Schedule (the renderer): when each queued frame becomes visible. The target is the
// frame's due time, but no earlier than the previous target plus the previous delay
// (the minimum stay, measured on the schedule and never on when a copy finished: that
// slipped every frame by the 7.5 ms copy, M1). The schedule carries durations exactly:
// a flip that lands up to one refresh period after its target keeps the schedule; only
// a present that missed by more than a period re-anchors it (anchoring on the copy's
// end drifted 1.2 % slow and ate the player's lead, M5). A new generation starts a new
// schedule at its first frame. Lateness counts only frames the player produced on time.
#pragma once

#include <algorithm>
#include <cstdint>

namespace p64::playback::timing {

constexpr uint32_t kMinFrameUs = 16667;  // the 60 fps presentation cap (spec 4.3)

class Timeline {
 public:
  struct Stamp {
    int64_t due_us;
    uint32_t delay_us;
    bool first;
    bool late;  // produced after its due time: the timeline re-anchored here
  };

  // A new source: its next frame is a first frame.
  void restart() { first_ = true; }
  bool first() const { return first_; }
  // The instant the next frame is for (0 for a first frame: "now"); a source that
  // renders time-dependent content (the clock) draws for this instant.
  int64_t next_due_us() const { return first_ ? 0 : next_due_us_; }

  // A frame was produced at `produced_us` with the stored delay `delay_ms`.
  Stamp produced(int64_t produced_us, uint32_t delay_ms) {
    Stamp s{};
    s.delay_us = std::max<uint32_t>(delay_ms * 1000u, kMinFrameUs);
    s.first = first_;
    if (first_) {
      s.due_us = produced_us;
    } else if (produced_us > next_due_us_) {
      s.due_us = produced_us;
      s.late = true;
    } else {
      s.due_us = next_due_us_;
    }
    next_due_us_ = s.due_us + s.delay_us;
    first_ = false;
    return s;
  }

 private:
  bool first_ = true;
  int64_t next_due_us_ = 0;
};

class Schedule {
 public:
  struct Plan {
    bool scheduled;       // the frame follows an earlier frame of its generation
    int64_t schedule_us;  // previous target + previous delay (when scheduled)
    int64_t target_us;    // when the frame should become visible
    int64_t start_at_us;  // when the copy into the driver should start
  };

  // Where a slot of `generation` due at `due_us` lands on the schedule.
  Plan plan(uint32_t generation, int64_t due_us) const {
    Plan p{};
    p.scheduled = generation == last_generation_ && last_visible_us_ != 0;
    p.schedule_us = p.scheduled ? last_visible_us_ + last_delay_us_ : due_us;
    p.target_us = std::max(due_us, p.schedule_us);
    p.start_at_us = p.target_us - copy_lead_us_;
    return p;
  }

  // The frame was copied in from `copy_start_us` to `copy_end_us` (the flip issued at
  // the end) with a refresh period of `period_us`. Returns whether it was late.
  bool presented(const Plan &p, uint32_t generation, uint32_t delay_us, bool decoded_late, int64_t copy_start_us,
                 int64_t copy_end_us, int64_t period_us) {
    // Rounded up: a truncating average settles a few microseconds below the copy time,
    // and when the refresh grid falls worst the flip then lands just past one period
    // after its target and the schedule slips a period (found by the host test of
    // 2026-09-22, about once in 1 760 frames at 25 fps).
    copy_lead_us_ = (copy_lead_us_ * 7 + (copy_end_us - copy_start_us) + 7) / 8;
    const int64_t visible = copy_end_us > p.target_us + period_us ? copy_end_us : p.target_us;
    const bool late = p.scheduled && !decoded_late && copy_end_us > p.schedule_us + period_us;
    last_visible_us_ = visible;
    last_delay_us_ = delay_us;
    last_generation_ = generation;
    return late;
  }

  int64_t copy_lead_us() const { return copy_lead_us_; }
  int64_t last_visible_us() const { return last_visible_us_; }

 private:
  int64_t last_visible_us_ = 0;
  int64_t copy_lead_us_ = 7500;  // running average of the copy time
  uint32_t last_delay_us_ = 0;
  uint32_t last_generation_ = 0;
};

}  // namespace p64::playback::timing

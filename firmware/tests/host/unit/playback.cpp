// Host unit tests: the ready-frame queue and the two timing rules (timing.hpp).
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


TEST_CASE("frame_queue") {
  static p64::playback::ReadySlot slots[p64::playback::FrameQueue::kSlots];
  p64::playback::FrameQueue q(slots);
  CHECK(q.consumer_peek() == nullptr);
  for (unsigned i = 0; i < p64::playback::FrameQueue::kSlots; ++i) {
    auto *s = q.producer_slot();
    CHECK(s != nullptr);
    s->generation = 1;
    s->due_us = i;
    q.producer_publish();
  }
  CHECK(q.producer_slot() == nullptr);
  CHECK_EQ(q.published(), p64::playback::FrameQueue::kSlots);
  auto *c = q.consumer_peek();
  CHECK((c != nullptr && c->due_us == 0));
  q.consumer_release();
  CHECK(q.producer_slot() != nullptr);
  auto *s = q.producer_slot();
  s->generation = 2;
  q.producer_publish();
  CHECK_EQ(q.latest_generation(), 2u);
  c = q.consumer_peek();
  CHECK((c != nullptr && c->due_us == 1 && c->generation == 1));
}

// --- the renderer's schedule and the player's timeline (timing.hpp) -----------------

using p64::playback::timing::Schedule;
using p64::playback::timing::Timeline;

constexpr int64_t kPeriodUs = 3686;  // Quality mode, 271.3 Hz
constexpr int64_t kCopyUs = 7600;    // the measured bit-plane copy at ten planes

// One presentation as the render task does it: wait until the copy start, then for the
// next refresh boundary, then copy for kCopyUs (the flip is issued at the copy's end).
struct Presented {
  Schedule::Plan plan;
  int64_t start, end;
  bool late;
};
Presented present(Schedule &s, int64_t &now, uint32_t gen, int64_t due, uint32_t delay_us, bool decoded_late = false,
                  int64_t extra_us = 0) {
  Presented p{};
  p.plan = s.plan(gen, due);
  if (now < p.plan.start_at_us) now = p.plan.start_at_us;
  now = ((now + kPeriodUs - 1) / kPeriodUs) * kPeriodUs;  // wait_for_back_buffer()
  p.start = now;
  now += kCopyUs + extra_us;
  p.end = now;
  p.late = s.presented(p.plan, gen, delay_us, decoded_late, p.start, p.end, kPeriodUs);
  return p;
}

TEST_CASE("schedule: durations carried exactly over 10000 frames (the M5 drift)") {
  Schedule s;
  int64_t now = 1000000;
  const uint32_t delay = 40000;  // a 25 fps APNG
  int64_t first_target = 0;
  int late = 0;
  for (int n = 0; n < 10000; ++n) {
    // The player is ahead: every frame is due well before the schedule wants it.
    const Presented p = present(s, now, 1, 1000000 + int64_t(n) * delay - 100000, delay);
    // The first frame anchors the schedule where it actually became visible; from then
    // on every target is exactly one delay after the previous one.
    if (n == 1) first_target = p.plan.target_us;
    if (n >= 1) {
      CHECK_EQ(p.plan.target_us, first_target + int64_t(n - 1) * delay);
      // The flip lands on the refresh grid within one period and one copy after the target.
      CHECK((p.end >= p.plan.target_us && p.end <= p.plan.target_us + kPeriodUs + kCopyUs + kPeriodUs));
    }
    if (p.late) ++late;
  }
  CHECK_EQ(late, 0);
}

TEST_CASE("schedule: the minimum stay is measured on the schedule, not from the copy's end (M1)") {
  Schedule s;
  int64_t now = 0;
  const uint32_t delay = 125000;  // 8 fps
  Presented a = present(s, now, 3, 0, delay);
  Presented b = present(s, now, 3, 0, delay);  // due long ago: only the stay holds it
  Presented c = present(s, now, 3, 0, delay);
  CHECK_EQ(b.plan.target_us, a.end + int64_t(delay));  // the first frame became visible at its copy's end
  CHECK_EQ(c.plan.target_us - b.plan.target_us, int64_t(delay));
  // Had the stay been counted from the copy's end, 8 fps would have become about 7.6.
  CHECK((c.end - a.end) < 2 * int64_t(delay) + 2 * kPeriodUs);
}

TEST_CASE("schedule: a present that misses by more than a period re-anchors, once") {
  Schedule s;
  int64_t now = 0;
  const uint32_t delay = 50000;
  present(s, now, 1, 0, delay);
  const Presented stalled = present(s, now, 1, 0, delay, false, 40000);  // the copy took 40 ms longer
  CHECK(stalled.late);
  const Presented next = present(s, now, 1, 0, delay);
  CHECK(!next.late);
  CHECK_EQ(next.plan.target_us, stalled.end + int64_t(delay));  // no catch-up
}

TEST_CASE("schedule: a frame the player produced late is not the renderer's lateness") {
  Schedule s;
  int64_t now = 0;
  present(s, now, 1, 0, 20000);
  const Presented p = present(s, now, 1, 0, 20000, true, 30000);
  CHECK(!p.late);
}

TEST_CASE("schedule: a new generation starts a new schedule at its due time") {
  Schedule s;
  int64_t now = 0;
  present(s, now, 1, 0, 1000000);  // a minute-long widget frame would hold the old schedule
  const Schedule::Plan p = s.plan(2, now + 5);
  CHECK(!p.scheduled);
  CHECK_EQ(p.target_us, now + 5);
}

TEST_CASE("schedule: the copy lead follows the measured copy time") {
  Schedule s;
  int64_t now = 0;
  for (int i = 0; i < 64; ++i) present(s, now, 1, 0, 20000);
  CHECK((s.copy_lead_us() > kCopyUs - 50 && s.copy_lead_us() < kCopyUs + 50));
}

TEST_CASE("timeline: first frame at once, then previous due plus delay") {
  Timeline t;
  CHECK_EQ(t.next_due_us(), 0);
  Timeline::Stamp a = t.produced(1000, 40);
  CHECK((a.first && !a.late));
  CHECK_EQ(a.due_us, 1000);
  CHECK_EQ(a.delay_us, 40000u);
  CHECK_EQ(t.next_due_us(), 41000);
  Timeline::Stamp b = t.produced(5000, 40);  // produced early: keeps the cadence
  CHECK((!b.first && !b.late));
  CHECK_EQ(b.due_us, 41000);
}

TEST_CASE("timeline: late frames re-anchor, nothing is skipped (ADR 0003)") {
  Timeline t;
  t.produced(0, 10);                          // 10 ms is below the 60 fps cap
  CHECK_EQ(t.next_due_us(), int64_t(p64::playback::timing::kMinFrameUs));
  Timeline::Stamp late = t.produced(30000, 10);  // a slow decode
  CHECK(late.late);
  CHECK_EQ(late.due_us, 30000);
  CHECK_EQ(t.next_due_us(), 30000 + int64_t(p64::playback::timing::kMinFrameUs));
  t.restart();
  CHECK(t.first());
  CHECK_EQ(t.next_due_us(), 0);
  CHECK(!t.produced(99999999, 100).late);  // a new source is never late
}

}  // namespace

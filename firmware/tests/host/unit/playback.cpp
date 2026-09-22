// Host unit tests: the ready-frame queue.
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

}  // namespace

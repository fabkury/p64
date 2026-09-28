// Host unit tests: the TLS slot's queue (p64_net/src/tls_slot.cpp, ADR 0009): a recursive
// lock handed to its waiters first come, first served.
#include "common.hpp"
#include "tls_slot.hpp"

namespace {

using p64::net::tls_slot::Queue;

const int kA = 1, kB = 2, kC = 3;
const void *const A = &kA;
const void *const B = &kB;
const void *const C = &kC;

TEST_CASE("tls slot: a free slot is granted at once and released back to free") {
  Queue q;
  CHECK(!q.contended());
  CHECK(q.request(A));
  CHECK(q.holder() == A);
  CHECK_EQ(q.depth(), 1u);
  CHECK(!q.contended());
  CHECK(q.release(A) == nullptr);
  CHECK(q.holder() == nullptr);
  CHECK_EQ(q.stats().grants, 1u);
  CHECK_EQ(q.stats().handoffs, 0u);
}

TEST_CASE("tls slot: the holder nests (a one-shot request inside a kept session)") {
  Queue q;
  REQUIRE(q.request(A));
  CHECK(q.request(A));
  CHECK_EQ(q.depth(), 2u);
  CHECK(q.request(B) == false);  // queued
  CHECK(q.contended());
  CHECK(q.release(A) == nullptr);  // still held once
  CHECK(q.holder() == A);
  CHECK(q.release(A) == B);  // the last release hands over
  CHECK(q.holder() == B);
  CHECK_EQ(q.depth(), 1u);
  CHECK_EQ(q.waiters(), 0u);
  CHECK_EQ(q.stats().handoffs, 1u);
}

TEST_CASE("tls slot: waiters are served in the order they asked, not by who asks again first") {
  Queue q;
  REQUIRE(q.request(A));
  CHECK(!q.request(B));
  CHECK(!q.request(C));
  CHECK_EQ(q.waiters(), 2u);
  CHECK(q.release(A) == B);
  // A asks again at once (the fetcher's next page): it queues behind C.
  CHECK(!q.request(A));
  CHECK(q.release(B) == C);
  CHECK(q.release(C) == A);
  CHECK(q.holder() == A);
  CHECK(q.release(A) == nullptr);
  CHECK_EQ(q.stats().waits, 3u);
  CHECK_EQ(q.stats().handoffs, 3u);
}

TEST_CASE("tls slot: asking twice while queued does not queue twice; a stray release is ignored") {
  Queue q;
  REQUIRE(q.request(A));
  CHECK(!q.request(B));
  CHECK(!q.request(B));
  CHECK_EQ(q.waiters(), 1u);
  CHECK(q.release(B) == nullptr);  // B does not hold it
  CHECK(q.holder() == A);
  CHECK(q.release(nullptr) == nullptr);
  CHECK(!q.request(nullptr));
  CHECK(q.release(A) == B);
  CHECK(q.release(B) == nullptr);
  q.note_wait_ms(120);
  q.note_wait_ms(40);
  CHECK_EQ(q.stats().max_wait_ms, 120u);
}

}  // namespace

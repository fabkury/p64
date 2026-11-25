// p64 -- the status push and a client that stopped reading (finding F-22, ADR 0019).
//
// The push is sent through the one HTTP task. A page that is open but no longer reads (a
// laptop asleep, a phone out of range) fills its window; a send to it then blocks that
// task until the send timeout (5 s), and returned "sent" as long as a part of the frame
// still fitted, so the same client stalled the server again at the next push: four times
// in a row on 2026-11-25, with every other page waiting and the healthy ones' pushes late.
//
// So nothing is sent to a client that cannot take it now. A client that cannot take
// kMaxMisses pushes in a row is closed; its page, if it is alive after all, falls back to
// polling and asks for the push again. Pure (no ESP-IDF include), host-tested
// (tests/host/unit/web.cpp).
#pragma once

namespace p64::web::push {

// Pushes a client may miss in a row: with one every 2 s, about 6 s without reading.
constexpr int kMaxMisses = 3;

enum class Action { Send, Skip, Close };

// What to do with one client at one push. `writable`: its socket can take data without
// blocking. `misses`: the pushes it has missed in a row, kept by the caller, updated here.
constexpr Action next(bool writable, int &misses) {
  if (writable) {
    misses = 0;
    return Action::Send;
  }
  return ++misses >= kMaxMisses ? Action::Close : Action::Skip;
}

}  // namespace p64::web::push

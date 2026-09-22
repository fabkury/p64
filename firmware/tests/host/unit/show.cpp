// Host unit tests: the show's decision rules (main/show_rules.cpp), with the scenarios of
// the bugs recorded in firmware/docs/PROGRESS.md replayed.
#include "common.hpp"
#include "show_rules.hpp"

#include <memory>

namespace {

namespace rules = p64::show::rules;

TEST_CASE("show: the swap timer never leaves an artwork frozen (the Widget-state bug of 2026-09-21)") {
  // An artwork put up while the main state still said Widget (a playset pill, a tap):
  // before the fix the timer keyed on the state and the artwork stayed for good.
  CHECK(rules::swap_timer_runs(false, /*artwork_up=*/true, /*widget_up=*/false, /*show_active=*/false));
  // The Widget state's own widget stays indefinitely; an interlude inside the show swaps.
  CHECK(!rules::swap_timer_runs(false, false, true, false));
  CHECK(rules::swap_timer_runs(false, false, true, true));
  // Paused: nothing swaps.
  CHECK(!rules::swap_timer_runs(true, true, false, true));
  // Nothing up: nothing to swap from.
  CHECK(!rules::swap_timer_runs(false, false, false, true));
}

TEST_CASE("show: the auto-swap falls due after the interval, never with an interval of 0") {
  CHECK(!rules::auto_swap_due(true, 30, 29999999, 0));
  CHECK(rules::auto_swap_due(true, 30, 30000000, 0));
  CHECK(!rules::auto_swap_due(false, 30, 99000000, 0));
  CHECK(!rules::auto_swap_due(true, 0, 99000000, 0));  // 0 = no auto-swap (spec 16)
}

TEST_CASE("show: interludes roll in the order clock, weather, temperature") {
  const uint8_t none[3] = {0, 0, 0};
  int calls = 0;
  CHECK_EQ(rules::roll_interlude(none, [&] { ++calls; return 0u; }), -1);
  CHECK_EQ(calls, 0);  // a 0 % widget is never rolled
  const uint8_t all[3] = {100, 100, 100};
  CHECK_EQ(rules::roll_interlude(all, [] { return 99u; }), 0);  // the clock wins first
  const uint8_t weather_only[3] = {0, 50, 0};
  CHECK_EQ(rules::roll_interlude(weather_only, [] { return 49u; }), 1);
  CHECK_EQ(rules::roll_interlude(weather_only, [] { return 50u; }), -1);
  const uint8_t two[3] = {10, 0, 10};
  uint32_t seq[] = {50, 5};
  int i = 0;
  CHECK_EQ(rules::roll_interlude(two, [&] { return seq[i++]; }), 2);  // clock loses, temperature wins
}

TEST_CASE("show: a pick prepared from a tiny cache is replaced as the cache grows (the one-file replay, M6)") {
  // One file cached: the prepared pick is the artwork on the panel.
  CHECK(rules::replace_prepared_pick(/*prepared*/ 7, /*pool at pick*/ 1, /*now*/ 1, /*up*/ true, /*current*/ 7));
  // Still one file, but a different artwork up: keep it.
  CHECK(!rules::replace_prepared_pick(7, 1, 1, true, 8));
  // The cache grew while the pool was small: choose again.
  CHECK(rules::replace_prepared_pick(7, 3, 4, true, 8));
  // A pick made from a healthy pool is kept however much the cache grows.
  CHECK(!rules::replace_prepared_pick(7, 8, 400, true, 8));
  // Nothing on the panel (a status screen): no repeat to avoid.
  CHECK(!rules::replace_prepared_pick(7, 8, 8, false, 7));
}

TEST_CASE("show: a fresh pick avoids the entry on the panel only within its channel and playset") {
  CHECK_EQ(rules::avoid_entry(true, 2, "Local", 5, 2, "Local"), 5);
  CHECK_EQ(rules::avoid_entry(true, 2, "Local", 5, 3, "Local"), -1);
  CHECK_EQ(rules::avoid_entry(true, 2, "Local", 5, 2, "Promoted"), -1);
  CHECK_EQ(rules::avoid_entry(false, 2, "Local", 5, 2, "Local"), -1);
}

TEST_CASE("show: channel status texts (spec 6.4)") {
  rules::ChannelFacts local;
  CHECK(rules::channel_status(local).empty());
  local.card_mounted = false;
  CHECK(rules::channel_status(local) == "no card");
  rules::ChannelFacts mk;
  mk.local = false;
  mk.needs_pairing = true;
  CHECK(rules::channel_status(mk) == "needs pairing");
  mk.paired = true;
  CHECK(rules::channel_status(mk) == "no listing yet");
  mk.online = false;
  CHECK(rules::channel_status(mk) == "offline");
  mk.index_entries = 50;
  CHECK(rules::channel_status(mk) == "downloading");
  mk.cached = 1;
  CHECK(rules::channel_status(mk).empty());
  rules::ChannelFacts future;
  future.supported = false;
  CHECK(rules::channel_status(future) == "not supported yet");
}

TEST_CASE("show: the no-artwork reason names the first obstacle") {
  CHECK(rules::no_artwork_reason({}) == "empty");
  CHECK(rules::no_artwork_reason({{"", 0}}) == "empty");
  CHECK(rules::no_artwork_reason({{"needs pairing", 0}, {"", 0}}) == "needs pairing");
  CHECK(rules::no_artwork_reason({{"", 0}, {"offline", 0}}) == "empty");
  CHECK(rules::no_artwork_reason({{"offline", 0}, {"", 3}}).empty());  // something can play
}

TEST_CASE("show: streams take the panel when allowed, after the boot animation and pairing") {
  CHECK(rules::stream_allowed(false, true));
  CHECK(rules::stream_allowed(true, false));  // the Stream state shows streams even with takeover off
  CHECK(!rules::stream_allowed(false, false));
  CHECK(rules::stream_gate(true, false, true, false, false) == rules::StreamGate::Take);
  CHECK(rules::stream_gate(true, false, true, true, false) == rules::StreamGate::Wait);
  CHECK(rules::stream_gate(true, false, true, false, true) == rules::StreamGate::Wait);
  CHECK(rules::stream_gate(true, true, true, false, false) == rules::StreamGate::No);
  CHECK(rules::stream_gate(true, false, false, false, false) == rules::StreamGate::No);
  CHECK(rules::stream_gate(false, false, true, false, false) == rules::StreamGate::No);
}

TEST_CASE("show: what the state puts up during a stream waits behind it and returns (spec 8.3)") {
  rules::Stage<std::shared_ptr<std::string>> stage;
  auto art1 = std::make_shared<std::string>("art1");
  auto art2 = std::make_shared<std::string>("art2");
  auto widget = std::make_shared<std::string>("clock");
  auto stream = std::make_shared<std::string>("stream");
  CHECK(stage.present(art1));  // plays
  stage.take(stream);
  CHECK(stage.stream_up());
  CHECK(*stage.on_panel() == "stream");
  CHECK(*stage.behind() == "art1");
  CHECK(!stage.present(art2));  // an auto-swap during the stream: parked, not played
  CHECK(!stage.present(widget));  // then an interlude: the newest parked wins
  CHECK(*stage.on_panel() == "stream");
  auto back = stage.release();
  REQUIRE(back);
  CHECK(*back == "clock");
  CHECK(!stage.stream_up());
  CHECK(!stage.behind());
  CHECK(stage.present(back));  // the show presents it again and it plays
  CHECK(*stage.on_panel() == "clock");
  // A stream that starts with nothing up returns nothing (the show then picks afresh).
  rules::Stage<std::shared_ptr<std::string>> empty;
  empty.take(stream);
  CHECK(!empty.release());
}

}  // namespace

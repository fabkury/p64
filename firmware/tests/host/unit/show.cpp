// Host unit tests: the show's decision rules (main/show_rules.cpp) and the show core
// (main/show_core.cpp) driven through whole scenarios by a fake ShowEnv, with the bugs
// recorded in firmware/docs/PROGRESS.md replayed.
#include "common.hpp"
#include "show_core.hpp"
#include "show_rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

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

TEST_CASE("show: the interlude plan turns median gaps into per-swap probabilities (ADR 0014)") {
  using rules::InterludeState;
  // The defaults on 30 s swaps: clock every 30 min, weather every 3 h, temperature never.
  const uint16_t defaults[4] = {30, 180, 0, 0};
  rules::InterludePlan p = rules::interlude_plan(defaults, 30);
  CHECK_EQ(p.count, 2);
  CHECK_EQ(p.order[0], 1);  // the larger median rolls first
  CHECK_EQ(p.order[1], 0);
  CHECK(p.state[0] == InterludeState::Rolled);
  CHECK(p.state[1] == InterludeState::Rolled);
  CHECK(p.state[2] == InterludeState::Never);
  CHECK(p.per_swap[0] == doctest::Approx(1.0 - std::exp2(-30.0 / 1800.0)).epsilon(1e-9));  // 1.149 %
  CHECK(p.per_swap[1] == doctest::Approx(1.0 - std::exp2(-30.0 / 10800.0)).epsilon(1e-9));  // 0.192 %
  CHECK_EQ(p.per_swap[2], 0.0);
  // The clock is rolled a little above its target: it loses the swaps the weather takes.
  CHECK(p.rolled[0] == doctest::Approx(p.per_swap[0] / (1.0 - p.per_swap[1])).epsilon(1e-9));
  CHECK(p.rolled[1] == doctest::Approx(p.per_swap[1]).epsilon(1e-9));
  // An hour on 30 s swaps: no interlude in 120 swaps with probability one half.
  const uint16_t hour[4] = {60, 0, 0, 0};
  p = rules::interlude_plan(hour, 30);
  CHECK(std::pow(1.0 - p.per_swap[0], 120.0) == doctest::Approx(0.5).epsilon(1e-9));
  // The interval equal to the gap: a coin toss.
  const uint16_t five[4] = {5, 0, 0, 0};
  CHECK(rules::interlude_plan(five, 300).per_swap[0] == doctest::Approx(0.5).epsilon(1e-9));
  // Longer than the gap: unsatisfiable, off; the other kinds unaffected.
  const uint16_t mixed[4] = {5, 180, 0, 0};
  p = rules::interlude_plan(mixed, 600);
  CHECK(p.state[0] == InterludeState::IntervalLonger);
  CHECK_EQ(p.per_swap[0], 0.0);
  CHECK(p.state[1] == InterludeState::Rolled);
  CHECK_EQ(p.count, 1);
  CHECK_EQ(p.order[0], 1);
  // No auto-swap: nothing rolls, and the status says why.
  p = rules::interlude_plan(defaults, 0);
  CHECK_EQ(p.count, 0);
  CHECK(p.state[0] == InterludeState::NoAutoSwap);
  CHECK(p.state[2] == InterludeState::Never);
  // Ties keep the fixed order clock, weather, temperature.
  const uint16_t tied[4] = {5, 5, 5, 0};
  p = rules::interlude_plan(tied, 60);
  CHECK_EQ(p.order[0], 0);
  CHECK_EQ(p.order[1], 1);
  CHECK_EQ(p.order[2], 2);
  CHECK(p.rolled[0] < p.rolled[1]);
  CHECK(p.rolled[1] < p.rolled[2]);
  // The air widget is the fourth kind (p074): last in a tie, first with the largest gap.
  const uint16_t four[4] = {5, 5, 5, 5};
  p = rules::interlude_plan(four, 60);
  CHECK_EQ(p.count, 4);
  CHECK_EQ(p.order[3], 3);
  CHECK(p.rolled[2] < p.rolled[3]);
  const uint16_t air_first[4] = {30, 180, 0, 240};
  p = rules::interlude_plan(air_first, 30);
  CHECK_EQ(p.count, 3);
  CHECK_EQ(p.order[0], 3);
  CHECK(p.per_swap[3] == doctest::Approx(1.0 - std::exp2(-30.0 / 14400.0)).epsilon(1e-9));
  // Three coin tosses: the third kind can get nothing, its roll caps at one.
  p = rules::interlude_plan(tied, 300);
  CHECK_EQ(p.rolled[0], 0.5);
  CHECK_EQ(p.rolled[1], 1.0);
  CHECK_EQ(p.rolled[2], 1.0);
}

TEST_CASE("show: the interlude roll takes the first winner in priority order") {
  const uint16_t none[4] = {0, 0, 0, 0};
  int calls = 0;
  CHECK_EQ(rules::roll_interlude(rules::interlude_plan(none, 30), [&] { ++calls; return 0u; }), -1);
  CHECK_EQ(calls, 0);  // a kind that is off is never rolled
  const uint16_t five[4] = {5, 0, 0, 0};
  const rules::InterludePlan coin = rules::interlude_plan(five, 300);  // 50 %
  CHECK_EQ(rules::roll_interlude(coin, [] { return 0x7fffffffu; }), 0);
  CHECK_EQ(rules::roll_interlude(coin, [] { return 0x80000000u; }), -1);
  // Clock 5 min and weather 180 min on 60 s swaps: the weather is asked first and wins
  // a coincidence; the clock gets the slot only when the weather lost.
  const uint16_t two[4] = {5, 180, 0, 0};
  const rules::InterludePlan plan = rules::interlude_plan(two, 60);
  CHECK_EQ(rules::roll_interlude(plan, [] { return 0u; }), 1);
  uint32_t seq[] = {0xffffffffu, 0u};
  int i = 0;
  CHECK_EQ(rules::roll_interlude(plan, [&] { return seq[i++]; }), 0);
  CHECK_EQ(rules::roll_interlude(plan, [] { return 0xffffffffu; }), -1);
}

TEST_CASE("show: a random clock face is uniform over the others and never the previous one (p057)") {
  // No previous face: all nine, each from its own residue.
  for (uint32_t r = 0; r < 9; ++r) CHECK_EQ(rules::random_face(255, r, 9), r);
  CHECK_EQ(rules::random_face(255, 9, 9), 0);
  // Previous face 4: the eight others, each exactly once over the eight residues.
  int seen[9] = {0};
  for (uint32_t r = 0; r < 8; ++r) ++seen[rules::random_face(4, r, 9)];
  CHECK_EQ(seen[4], 0);
  for (int f = 0; f < 9; ++f) {
    if (f != 4) CHECK_EQ(seen[f], 1);
  }
  // The ends: previous 0 never gives 0, previous 8 never gives 8.
  for (uint32_t r = 0; r < 16; ++r) {
    CHECK_NE(rules::random_face(0, r, 9), 0);
    CHECK_NE(rules::random_face(8, r, 9), 8);
    CHECK_LT(rules::random_face(8, r, 9), 9);
  }
  CHECK_EQ(rules::random_face(0, 7, 1), 0);  // one face: always it
}

TEST_CASE("show: over many swaps every kind's realised rate and median gap match its setting") {
  // A seeded generator (the 64-bit LCG of Knuth's MMIX, upper 32 bits) drives the same
  // roll the device makes; the maths is the one tools/interlude_sim.py checked offline.
  uint64_t x = 20260928;
  auto rnd = [&] {
    x = x * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(x >> 32);
  };
  const uint16_t minutes[4] = {5, 5, 20, 0};
  const uint32_t interval_s = 60;
  const rules::InterludePlan plan = rules::interlude_plan(minutes, interval_s);
  const int swaps = 400000;
  int count[3] = {0, 0, 0}, last[3] = {-1, -1, -1};
  std::vector<int> gaps[3];
  for (int n = 0; n < swaps; ++n) {
    const int w = rules::roll_interlude(plan, rnd);
    if (w < 0) continue;
    ++count[w];
    if (last[w] >= 0) gaps[w].push_back(n - last[w]);
    last[w] = n;
  }
  for (int k = 0; k < 3; ++k) {
    // The realised rate is the target within four binomial sigmas: the compensation
    // for the slots the higher kinds take is exact (without it the 5-min kind rolled
    // second ran 13 % slow in the offline check).
    const double p = 1.0 - std::exp2(-static_cast<double>(interval_s) / (60.0 * minutes[k]));
    const double sigma = std::sqrt(p * (1 - p) / swaps);
    CHECK(std::fabs(count[k] / static_cast<double>(swaps) - p) < 4 * sigma);
    // The sample median gap is the setting, within one interval (the discrete median
    // sits between M/T and M/T + 1 swaps).
    std::sort(gaps[k].begin(), gaps[k].end());
    const double median_min = gaps[k][gaps[k].size() / 2] * interval_s / 60.0;
    CHECK(median_min >= minutes[k]);
    CHECK(median_min <= minutes[k] + interval_s / 60.0);
  }
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
  // A listing that landed empty is not a missing listing (the @Sendew channel of
  // 2026-09-22: an artist with no posts read "no listing yet" after a good refresh).
  mk.refreshed = true;
  CHECK(rules::channel_status(mk) == "no artworks");
  mk.oversized = 3;
  mk.min_side = 16;
  mk.max_side = 128;
  CHECK(rules::channel_status(mk) == "nothing fits 16 to 128 px (3 outside)");
  mk.online = false;
  CHECK(rules::channel_status(mk) == "offline");
  mk.refreshed = false;
  mk.oversized = 0;
  mk.index_entries = 50;
  CHECK(rules::channel_status(mk) == "offline");  // an index loaded before the network: nothing downloads yet
  mk.online = true;
  CHECK(rules::channel_status(mk) == "downloading");
  // A provider still checking its cached files after a boot, holding them back, is not
  // downloading (the Divoom channel of 2026-09-26); one that offers them meanwhile plays.
  mk.unchecked = 50;
  CHECK(rules::channel_status(mk) == "checking files");
  mk.cached = 1;
  CHECK(rules::channel_status(mk).empty());
  mk.unchecked = 0;
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

TEST_CASE("show: the connected screen waits for the boot animation, then shows only when nothing is up or coming") {
  CHECK(rules::connected_screen(true, false, false) == rules::ConnectedScreen::Wait);
  CHECK(rules::connected_screen(true, true, true) == rules::ConnectedScreen::Wait);
  CHECK(rules::connected_screen(false, true, false) == rules::ConnectedScreen::Skip);
  CHECK(rules::connected_screen(false, false, true) == rules::ConnectedScreen::Skip);
  CHECK(rules::connected_screen(false, false, false) == rules::ConnectedScreen::Show);
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

// --- the show core through whole scenarios ------------------------------------------

namespace core = p64::show::core;
using p64::playback::FrameSource;
using p64::gfx::Frame;
using p64::gfx::Rgb;

std::vector<uint8_t> corpus_gif() {
  std::ifstream in(std::string(P64_HOST_TESTS_DIR) + "/corpus/gif_anim_32.gif", std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A world whose loader and scanner answer when the test says so.
class FakeEnv : public p64::show::ShowEnv {
 public:
  int64_t now = 1000000;
  std::shared_ptr<p64::system::Settings> cfg = std::make_shared<p64::system::Settings>();
  std::vector<std::string> played;  // names of the sources handed to the player, in order
  std::string persisted_playset;
  struct Load {
    uint32_t id;
    std::string path;
  };
  std::vector<Load> loads;
  std::vector<std::pair<uint32_t, p64::content::Playset>> scans;
  std::set<std::string> missing_files;
  uint32_t next_id = 1;
  int swapped_events = 0;

  int64_t now_us() override { return now; }
  uint32_t random() override { return rng = rng * 1664525u + 1013904223u; }
  std::shared_ptr<const p64::system::Settings> settings() override { return cfg; }
  void persist_main_state(p64::system::MainState state) override { cfg->main_state = state; }
  void persist_active_playset(const std::string &name) override { persisted_playset = name; }
  bool load_playset(const std::string &, p64::content::Playset &, std::string &error) override {
    error = "no such playset";
    return false;
  }
  void play(std::shared_ptr<FrameSource> source) override { played.push_back(source ? source->name() : "(null)"); }
  std::shared_ptr<FrameSource> static_frame(const char *name, const Frame &frame) override {
    return std::make_shared<p64::playback::StaticSource>(name, frame);
  }
  std::shared_ptr<FrameSource> widget(p64::system::WidgetKind kind, int face) override {
    return std::make_shared<p64::playback::StaticSource>(
        std::string("widget:") + widget_name(kind) + (face < 0 ? "" : ":" + std::to_string(face)), Frame());
  }
  const char *widget_name(p64::system::WidgetKind kind) override {
    return kind == p64::system::WidgetKind::Weather ? "weather"
           : kind == p64::system::WidgetKind::Temperature ? "temperature"
           : kind == p64::system::WidgetKind::Air ? "air" : "clock";
  }
  std::shared_ptr<FrameSource> stream_source() override {
    if (!stream) stream = std::make_shared<p64::playback::StaticSource>("stream", Frame());
    return stream;
  }
  void stream_wake() override {}
  p64::stream::Status stream_status() override { return {}; }
  RenderTotals render_totals() override { return {}; }
  uint32_t load(const std::string &path, Rgb) override {
    loads.push_back({next_id, path});
    return next_id++;
  }
  void scan(uint32_t generation, const p64::content::Playset &playset) override { scans.emplace_back(generation, playset); }
  bool card_mounted() override { return true; }
  std::string storage_root() override { return "/sd/p64"; }
  std::string animations_dir() override { return "/sd/p64/animations"; }
  std::string downloads_dir() override { return "/sd/p64/downloads"; }
  p64::makapix::Status makapix_status() override { return {}; }
  bool makapix_paired() override { return false; }
  bool makapix_play_followed(std::string &error) override {
    error = "not paired";
    return false;
  }
  p64::net::wifi::Status wifi = [] {
    p64::net::wifi::Status w;
    w.connected = true;
    w.network_saved = true;
    return w;
  }();
  UpdateState update;
  p64::net::wifi::Status wifi_status() override { return wifi; }
  UpdateState update_state() override { return update; }
  void playback_swapped(int32_t) override { ++swapped_events; }
  void notify_web() override {}
  void vlog(char, const char *, va_list) override {}

  // The loader finishes every request made so far (a file named in missing_files fails).
  void complete_loads() {
    std::vector<Load> batch;
    batch.swap(loads);
    for (const Load &l : batch) {
      auto r = std::make_unique<p64::loader::LoadResult>();
      r->id = l.id;
      r->path = l.path;
      if (missing_files.count(l.path)) {
        r->error = "no such file";
        r->missing = true;
      } else {
        auto art = std::make_shared<p64::playback::Artwork>();
        std::string error;
        const std::string name = l.path.substr(l.path.rfind('/') + 1);
        REQUIRE(art->open(corpus_gif(), name, Rgb{0, 0, 0}, error));
        r->artwork = art;
      }
      core::on_loaded(std::move(r));
    }
  }
  // The scanner finishes the last scan with `files` in the playset's first (local) channel.
  void complete_scan(const std::vector<std::string> &files) {
    REQUIRE(!scans.empty());
    auto r = std::make_unique<p64::loader::ScanResult>();
    r->generation = scans.back().first;
    r->playset = scans.back().second;
    scans.clear();
    r->entries.resize(r->playset.channels.size());
    r->errors.resize(r->playset.channels.size());
    uint32_t mtime = 1000;
    for (const std::string &f : files) {
      p64::content::LocalEntry e{};
      std::snprintf(e.name, sizeof e.name, "%s", f.c_str());
      e.mtime = mtime--;
      r->entries[0].push_back(e);
    }
    core::on_scanned(std::move(r));
  }
  std::string last_played() const { return played.empty() ? "" : played.back(); }

 private:
  uint32_t rng = 12345;
  std::shared_ptr<FrameSource> stream;
};

// A content provider with one channel, "pics", whose items live under a cache folder
// (ADR 0012). The test decides what it can play and watches what the show reports.
class FakeProvider : public p64::content::Provider {
 public:
  std::vector<int32_t> items = {1, 2, 3};
  bool online = true, authorized = true;
  std::vector<std::string> active;   // channel identifiers the show set active
  std::vector<int32_t> shown;        // note_shown ids, in order
  std::vector<int32_t> failed;       // note_load_failed ids
  int hidden = 0;
  uint16_t side = 32;
  uint32_t extra_listed = 0;  // listed but not at hand (still downloading)
  uint32_t unchecked = 0;     // a post-load file check still running over this many entries
  std::string name;           // every item's name when set (else "pic <id>")

  const char *id() const override { return "fake"; }
  const char *label() const override { return "Fake source"; }
  bool owns(const p64::content::ChannelSpec &) const override { return false; }
  State state() override { return {online, authorized}; }
  void set_active_channels(const std::vector<p64::content::ChannelRef> &refs) override {
    active.clear();
    for (const auto &r : refs) active.push_back(r.identifier);
  }
  bool snapshot(const p64::content::ChannelRef &ref, p64::content::ChannelSnapshot &out) override {
    if (ref.identifier != "pics") return false;
    for (int32_t id : items) out.items.push_back({id, side, side});
    out.listed = static_cast<uint32_t>(items.size()) + extra_listed;
    out.last_refresh = 1700000000;
    out.unchecked = unchecked;
    return true;
  }
  bool resolve(const p64::content::ChannelRef &, const p64::content::ProviderItem &item, std::string &path,
               std::string &name) override {
    path = "/sd/p64/cache/fake/" + std::to_string(item.id) + ".gif";
    name = this->name.empty() ? "pic " + std::to_string(item.id) : this->name;
    return true;
  }
  void note_load_failed(const p64::content::ChannelRef &, const p64::content::ProviderItem &item, bool) override {
    failed.push_back(item.id);
    items.erase(std::remove(items.begin(), items.end(), item.id), items.end());
  }
  void note_shown(int32_t id, const p64::content::ChannelRef *, bool) override { shown.push_back(id); }
  void note_hidden() override { ++hidden; }
  std::vector<p64::content::ChannelOffer> offers() override { return {{"pics", "The pictures"}}; }
};

p64::content::Playset external_playset(const std::string &identifier = "fake:pics") {
  p64::content::Playset p;
  p.name = "ext";
  p64::content::ChannelSpec c;
  c.kind = p64::content::ChannelKind::External;
  c.identifier = identifier;
  p.channels.push_back(c);
  return p;
}

// A show restored on the Local playset with three files, the first artwork on the panel.
struct Show {
  FakeEnv env;
  Frame scratch;
  Show() {
    core::init(env, scratch);
    core::restore("Local");
    env.complete_scan({"a.gif", "b.gif", "c.gif"});
    env.complete_loads();  // the first pick plays, the next one is requested
    REQUIRE(core::state().current);
    env.complete_loads();  // ...and prepared
  }
  std::string current() const { return core::state().current ? core::state().current->name() : ""; }
  void advance_s(int seconds) {
    env.now += int64_t(seconds) * 1000000;
    core::tick();
  }
};

TEST_CASE("show core: restore scans the playset, plays a pick and prepares the next") {
  Show show;
  CHECK(core::active_playset_name() == "Local");
  CHECK_EQ(core::state().history.size(), 1u);
  CHECK(show.env.last_played() == show.current());
  CHECK(core::state().prepared);  // the next artwork is ready before it is due (spec 3.6)
  CHECK(core::overlay_allowed());
}

// The 18 s boot of 2026-09-26: the IP landed during the boot animation, nothing was
// "playing yet", and the connected screen held the panel for its 15 s while the first
// artwork sat prepared. A normal boot goes boot animation, then artwork (spec 15.1).
TEST_CASE("show core: an IP that lands during the boot animation never delays the first artwork (2026-09-26)") {
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::boot(std::make_shared<p64::playback::StaticSource>("boot", Frame()), 3000);
  core::restore("Local");
  env.complete_scan({"a.gif", "b.gif"});
  env.complete_loads();  // the first pick is prepared, but the boot animation holds
  CHECK(!core::state().current);
  CHECK(core::state().prepared);
  core::wifi_connected();  // the IP, 1 s into the animation
  core::tick();
  CHECK(core::state().screen == core::Screen::None);
  CHECK(env.last_played() == "boot");
  env.now += 2999000;
  core::tick();
  CHECK(env.last_played() == "boot");
  env.now += 1000;  // the animation ends: the artwork, not the IP
  core::tick();
  REQUIRE(core::state().current);
  CHECK(core::state().screen == core::Screen::None);
  CHECK(std::count(env.played.begin(), env.played.end(), "status") == 0);
  CHECK_EQ(core::state().first_artwork_us, env.now);
  cJSON *st = core::status_json();
  cJSON *boot = cJSON_GetObjectItem(st, "boot");
  REQUIRE(boot);
  CHECK_EQ(static_cast<int64_t>(cJSON_GetObjectItem(boot, "first_artwork_ms")->valuedouble), env.now / 1000);
  CHECK_EQ(static_cast<int64_t>(cJSON_GetObjectItem(boot, "animation_end_ms")->valuedouble), core::state().boot_until_us / 1000);
  cJSON_Delete(st);
  // An artwork that is still loading when the animation ends counts as on its way too.
  core::init(env, scratch);
  env.played.clear();
  core::boot(std::make_shared<p64::playback::StaticSource>("boot", Frame()), 3000);
  core::restore("Local");
  env.complete_scan({"a.gif"});  // the load is requested, not answered yet
  core::wifi_connected();
  env.now += 3000000;
  core::tick();
  CHECK(core::state().screen == core::Screen::None);
  env.complete_loads();
  CHECK(core::state().current);
  CHECK(std::count(env.played.begin(), env.played.end(), "status") == 0);
}

TEST_CASE("show core: with nothing to play at the boot animation's end the IP shows, then yields to the first artwork") {
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::boot(std::make_shared<p64::playback::StaticSource>("boot", Frame()), 3000);
  core::restore("Local");
  env.complete_scan({});  // an empty folder: "no artwork" waits behind the animation
  core::wifi_connected();
  core::tick();
  CHECK(core::state().screen == core::Screen::None);  // the animation still runs
  CHECK(env.last_played() == "boot");                 // and nothing cut it short
  env.now += 3000000;
  core::tick();
  CHECK(core::state().screen == core::Screen::Connected);
  CHECK(env.last_played() == "status");
  CHECK(std::count(env.played.begin(), env.played.end(), "no artwork") == 1);  // the reason went up first, at the animation's end
  // A file appears and the rescan finds it: the pick is prepared behind the screen...
  core::refresh();
  core::tick();
  env.complete_scan({"late.gif"});
  env.complete_loads();
  CHECK(core::state().prepared);
  CHECK(core::state().screen == core::Screen::Connected);  // ...readable for its minimum stay...
  env.now += 1999000;
  core::tick();
  CHECK(core::state().screen == core::Screen::Connected);
  env.now += 1000;
  core::tick();  // ...then the artwork replaces it, well before the 15 s
  CHECK(core::state().screen == core::Screen::None);
  REQUIRE(core::state().current);
  CHECK(core::state().current->name() == "late.gif");
  CHECK_EQ(core::state().first_artwork_us, env.now);
  // A later reconnection while an artwork plays shows nothing.
  core::wifi_connected();
  core::tick();
  CHECK(core::state().screen == core::Screen::None);
}

TEST_CASE("show core: the auto-swap puts the prepared artwork up after the interval") {
  Show show;
  show.env.cfg->auto_swap_seconds = 30;
  const std::string first = show.current();
  show.advance_s(29);
  CHECK(show.current() == first);
  show.advance_s(1);
  CHECK(show.current() != first);
  CHECK_EQ(core::state().history.size(), 2u);
  CHECK_EQ(show.env.loads.size(), 1u);  // the one after is being prepared
}

TEST_CASE("show core: a playset request in the Widget state leaves no artwork frozen (2026-09-21)") {
  Show show;
  show.env.cfg->main_state = p64::system::MainState::Widget;
  core::settings_changed();
  CHECK(show.env.last_played() == "widget:clock");
  CHECK(!core::overlay_allowed());
  // The Playsets pill on the Home page: the user wants artworks again.
  core::activate("Local");
  CHECK(show.env.cfg->main_state == p64::system::MainState::AnimationShow);  // persisted
  show.env.complete_scan({"a.gif", "b.gif", "c.gif"});
  show.env.complete_loads();
  REQUIRE(core::state().current);
  const std::string up = show.current();
  show.env.complete_loads();
  show.env.cfg->auto_swap_seconds = 10;
  show.advance_s(10);
  CHECK(show.current() != up);  // the timer runs: the artwork does not stay for good
}

TEST_CASE("show core: a resume that lands after something else went up is ignored (M6 stale resume)") {
  Show show;
  const std::string a = show.current();
  core::makapix_state(p64::makapix::State::Pairing);  // the pairing code takes the panel
  CHECK(!core::state().current);
  core::makapix_state(p64::makapix::State::Paired);   // "paired" for 10 s
  show.advance_s(10);                                 // the screen ends: resume of `a` requested
  REQUIRE(!show.env.loads.empty());
  const auto resume = show.env.loads.back();
  show.env.loads.pop_back();
  core::next();                                       // meanwhile Next puts the prepared one up
  const std::string b = show.current();
  REQUIRE(!b.empty());
  CHECK(b != a);
  show.env.loads.push_back(resume);                   // now the resume result lands
  show.env.complete_loads();
  CHECK(show.current() == b);                         // ...and is ignored
  CHECK(core::state().history.current()->name == b);  // history and panel agree
}

TEST_CASE("show core: a settings change in the Widget state redraws the widget at once (2026-09-20)") {
  Show show;
  show.env.cfg->main_state = p64::system::MainState::Widget;
  core::settings_changed();
  const size_t n = show.env.played.size();
  show.env.cfg->clock.face = p64::system::ClockFace::Analogue;  // the face changed on the Settings page
  core::settings_changed();
  CHECK_EQ(show.env.played.size(), n + 1);  // restarted, not left until its next minute
  CHECK(show.env.last_played() == "widget:clock");
}

TEST_CASE("show core: what the show puts up during a stream waits and returns when it ends (spec 8.3)") {
  Show show;
  show.env.cfg->auto_swap_seconds = 30;
  const std::string a = show.current();
  core::stream_started();
  CHECK(show.env.last_played() == "stream");
  CHECK(!core::overlay_allowed());
  show.advance_s(30);  // the auto-swap runs on invisibly
  CHECK(show.env.last_played() == "stream");
  const std::string b = show.current();
  CHECK(b != a);
  core::stream_ended();
  CHECK(show.env.last_played() == b);  // back to what the show had put up meanwhile
  CHECK(core::overlay_allowed());
}

TEST_CASE("show core: pause shows black and stops the timer; resume brings the artwork back") {
  Show show;
  show.env.cfg->auto_swap_seconds = 5;
  const std::string a = show.current();
  core::pause();
  CHECK(core::is_paused());
  CHECK(show.env.last_played() == "paused");
  show.advance_s(60);
  CHECK(!core::state().current);  // nothing swapped while paused
  core::resume();
  show.env.complete_loads();
  CHECK(!core::is_paused());
  CHECK(show.current() == a);
}

TEST_CASE("show core: a missing file is marked and another pick plays") {
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::restore("Local");
  env.missing_files = {"/sd/p64/animations/a.gif", "/sd/p64/animations/b.gif"};
  env.complete_scan({"a.gif", "b.gif", "c.gif"});
  for (int i = 0; i < 6 && !core::state().current; ++i) env.complete_loads();
  REQUIRE(core::state().current);
  CHECK(core::state().current->name() == "c.gif");
  CHECK(core::state().load_failures >= 1);
}

TEST_CASE("show core: a provider channel plays its items and hears what happened (ADR 0012)") {
  namespace providers = p64::content::providers;
  providers::clear();
  FakeProvider fake;
  fake.extra_listed = 1;
  providers::add(&fake);
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::activate_transient(external_playset());
  env.complete_scan({});  // no local channel: the scan brings nothing
  REQUIRE_EQ(fake.active.size(), 1u);
  CHECK(fake.active[0] == "pics");  // the provider sees its own identifier, without the prefix
  const core::State &st = core::state();
  REQUIRE_EQ(st.channels.size(), 1u);
  CHECK(st.channels[0].provider == &fake);
  CHECK_EQ(st.channels[0].available, 3u);
  CHECK(st.channels[0].status.empty());
  CHECK(st.channels[0].listed == 4u);
  env.complete_loads();
  REQUIRE(st.current);
  REQUIRE_EQ(fake.shown.size(), 1u);
  const p64::content::HistoryItem *cur = st.history.current();
  REQUIRE(cur);
  CHECK(cur->name == "pic " + std::to_string(fake.shown[0]));  // the provider's name for the item
  CHECK(cur->path == "/sd/p64/cache/fake/" + std::to_string(fake.shown[0]) + ".gif");
  CHECK(cur->provider == "fake");
  CHECK(cur->item_id == fake.shown[0]);
  CHECK(cur->channel == "The pictures");  // the provider's label for the channel
  CHECK(core::current_post_id() == -1);   // not a Makapix post
  // The channels document names the provider and its counts.
  cJSON *doc = core::channels_json();
  cJSON *ch = cJSON_GetArrayItem(cJSON_GetObjectItem(doc, "channels"), 0);
  CHECK(std::string(cJSON_GetStringValue(cJSON_GetObjectItem(ch, "provider"))) == "fake");
  CHECK(std::string(cJSON_GetStringValue(cJSON_GetObjectItem(ch, "kind"))) == "external");
  CHECK(cJSON_GetObjectItem(ch, "cached")->valueint == 3);
  CHECK(cJSON_GetObjectItem(ch, "entries")->valueint == 4);
  cJSON_Delete(doc);
  // A file that fails to load is reported to the provider, which drops it; another plays.
  env.complete_loads();  // the prepared one
  const int32_t prepared_id = st.prepared_pick.item.id;
  env.missing_files.insert("/sd/p64/cache/fake/" + std::to_string(prepared_id) + ".gif");
  core::refresh();  // drop the prepared artwork and pick again
  for (int i = 0; i < 6 && fake.failed.empty(); ++i) {
    env.complete_loads();
    core::next();
  }
  CHECK(!fake.failed.empty());
  CHECK(std::find(fake.items.begin(), fake.items.end(), fake.failed[0]) == fake.items.end());
  // The size limit applies to provider items too: nothing fits, the channel is out of
  // play (its index still lists the items, so it reads as still downloading).
  env.cfg->makapix_max_side = 16;
  core::provider_changed();
  CHECK_EQ(st.channels[0].available, 0u);
  CHECK(st.channels[0].status == "downloading");
  // And the minimum (2026-09-28): the items are 32 px, a minimum of 64 leaves none.
  env.cfg->makapix_max_side = 256;
  env.cfg->makapix_min_side = 64;
  core::provider_changed();
  CHECK_EQ(st.channels[0].available, 0u);
  env.cfg->makapix_min_side = 32;
  core::provider_changed();
  CHECK(st.channels[0].available > 0u);
  providers::clear();
}

TEST_CASE("show core: a provider's name cut inside a UTF-8 character reaches the history whole (2026-10-01)") {
  // A Divoom title cut by a 36-byte field ended on a lone lead byte and made
  // /api/v1/history unreadable; the show keeps only valid UTF-8 from a provider.
  namespace providers = p64::content::providers;
  providers::clear();
  FakeProvider fake;
  fake.name = "\xd0\xa7\xd0\xb8\xd0\xba\xd0\xb5\xd0\xbd \xd1\x87\xd0\xb5\xd0";  // a Cyrillic title cut after a lead byte
  providers::add(&fake);
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::activate_transient(external_playset());
  env.complete_scan({});
  env.complete_loads();
  const p64::content::HistoryItem *cur = core::state().history.current();
  REQUIRE(cur);
  CHECK(cur->name == "\xd0\xa7\xd0\xb8\xd0\xba\xd0\xb5\xd0\xbd \xd1\x87\xd0\xb5");
  cJSON *doc = core::history_json();
  char *text = cJSON_PrintUnformatted(doc);
  CHECK(p64::content::valid_utf8(text) == text);
  cJSON_free(text);
  cJSON_Delete(doc);
  providers::clear();
}

TEST_CASE("show core: a provider checking its files after a boot keeps playing (2026-09-26)") {
  // The Divoom channel read "downloading" with 0 available for half an hour after every
  // boot: its provider offered only the entries its file check had passed, and the show
  // heard nothing while the check ran. The contract now says the check is informative:
  // the cached items are offered and the count left is reported.
  namespace providers = p64::content::providers;
  providers::clear();
  FakeProvider fake;
  fake.unchecked = 3;
  providers::add(&fake);
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::activate_transient(external_playset());
  env.complete_scan({});
  const core::State &st = core::state();
  REQUIRE_EQ(st.channels.size(), 1u);
  CHECK_EQ(st.channels[0].available, 3u);
  CHECK(st.channels[0].status.empty());
  CHECK_EQ(st.channels[0].unchecked, 3u);
  cJSON *doc = core::channels_json();
  cJSON *ch = cJSON_GetArrayItem(cJSON_GetObjectItem(doc, "channels"), 0);
  CHECK(cJSON_GetObjectItem(ch, "unchecked")->valueint == 3);
  CHECK(std::string(cJSON_GetStringValue(cJSON_GetObjectItem(ch, "status"))).empty());
  cJSON_Delete(doc);
  env.complete_loads();
  CHECK(st.current);  // an artwork went up while the check runs
  // The check ends: the count drops to 0 and nothing else changes.
  fake.unchecked = 0;
  core::provider_changed();
  CHECK_EQ(st.channels[0].unchecked, 0u);
  CHECK_EQ(st.channels[0].available, 3u);
  providers::clear();
}

TEST_CASE("show core: a provider channel nobody serves, or without credentials, says so") {
  namespace providers = p64::content::providers;
  providers::clear();
  FakeEnv env;
  Frame scratch;
  core::init(env, scratch);
  core::activate_transient(external_playset("nobody:pics"));
  env.complete_scan({});
  REQUIRE_EQ(core::state().channels.size(), 1u);
  CHECK(core::state().channels[0].provider == nullptr);
  CHECK(core::state().channels[0].status == "not supported yet");
  FakeProvider fake;
  fake.authorized = false;
  providers::add(&fake);
  core::activate_transient(external_playset());
  env.complete_scan({});
  CHECK(core::state().channels[0].status == "needs pairing");
  fake.authorized = true;
  fake.online = false;
  fake.items.clear();
  core::provider_changed();
  CHECK(core::state().channels[0].status == "offline");
  providers::clear();
}

TEST_CASE("show core: history navigation goes back and forward through what was shown") {
  Show show;
  show.env.cfg->auto_swap_seconds = 5;
  const std::string a = show.current();
  show.advance_s(5);
  show.env.complete_loads();
  const std::string b = show.current();
  REQUIRE(b != a);
  core::previous();
  show.env.complete_loads();
  CHECK(show.current() == a);
  core::next();
  show.env.complete_loads();
  CHECK(show.current() == b);
  CHECK_EQ(core::state().history.size(), 2u);
}


// --- the Setup and Update screens (spec 6.4; settled 2026-09-23) ---------------------

TEST_CASE("show rules: the setup screen holds without a saved network, else replaces only 'no artwork'") {
  CHECK(rules::setup_screen(false, false) == rules::SetupScreen::None);
  CHECK(rules::setup_screen(false, true) == rules::SetupScreen::None);
  CHECK(rules::setup_screen(true, false) == rules::SetupScreen::Holds);
  CHECK(rules::setup_screen(true, true) == rules::SetupScreen::InsteadOfNoArtwork);
}

TEST_CASE("show core: with no network saved, the Setup screen holds the panel and turns its pages") {
  Show show;
  const std::string a = show.current();
  show.env.wifi.connected = false;
  show.env.wifi.network_saved = false;
  show.env.wifi.setup_mode = true;
  show.env.wifi.ap_ssid = "p64-setup";
  show.env.wifi.ap_ip = "192.168.4.1";
  show.advance_s(0);
  CHECK(core::state().screen == core::Screen::Setup);
  CHECK(show.env.last_played() == "setup");
  CHECK(!core::overlay_allowed());
  const size_t presents = show.env.played.size();
  show.advance_s(1);
  CHECK_EQ(show.env.played.size(), presents);  // a page stays 3 s
  show.advance_s(2);
  CHECK_EQ(show.env.played.size(), presents + 1);  // the next page
  CHECK_EQ(core::state().setup_page, 1);
  show.env.cfg->auto_swap_seconds = 5;
  show.advance_s(3);
  CHECK(core::state().screen == core::Screen::Setup);  // the auto-swap does not take the panel back
  // A network is saved and joined: setup mode ends, the artwork comes back.
  show.env.wifi.setup_mode = false;
  show.env.wifi.network_saved = true;
  show.env.wifi.connected = true;
  show.advance_s(0);
  CHECK(core::state().screen == core::Screen::None);
  show.env.complete_loads();
  CHECK(show.current() == a);
}

TEST_CASE("show core: with a saved network down, artworks keep playing; the setup pages replace only 'no artwork'") {
  Show show;
  show.env.wifi.connected = false;
  show.env.wifi.setup_mode = true;  // network_saved stays true
  const std::string a = show.current();
  show.advance_s(1);
  CHECK(core::state().screen == core::Screen::None);
  CHECK(show.current() == a);
  // Nothing to play: the setup pages instead of "no artwork".
  FakeEnv env;
  Frame scratch;
  env.wifi.connected = false;
  env.wifi.setup_mode = true;
  core::init(env, scratch);
  core::restore("Local");
  env.complete_scan({});
  CHECK(!core::state().current);
  CHECK(env.last_played() == "setup");
  CHECK(core::state().setup_pages_up);
  env.now += 3 * 1000000;
  core::tick();
  CHECK_EQ(core::state().setup_page, 1);
  // Setup mode ends without anything to play: the reason comes back.
  env.wifi.setup_mode = false;
  env.now += 1000000;
  core::tick();
  CHECK(env.last_played() == "no artwork");
  CHECK(!core::state().setup_pages_up);
}

TEST_CASE("show core: the Update screen follows the install and the artwork returns after a failure") {
  using P = p64::show::ShowEnv::UpdateState::Phase;
  Show show;
  const std::string a = show.current();
  // A failed release check never had the screen up: nothing shows.
  show.env.update.phase = P::Failed;
  show.env.update.error = "no release published";
  show.advance_s(1);
  CHECK(core::state().screen == core::Screen::None);
  CHECK(show.current() == a);
  // An install: progress while it downloads, redrawn when the percentage moves.
  show.env.update = {};
  show.env.update.phase = P::Downloading;
  show.env.update.version = "0.2.0";
  show.advance_s(1);
  CHECK(core::state().screen == core::Screen::Update);
  CHECK(show.env.last_played() == "update");
  const size_t presents = show.env.played.size();
  show.advance_s(1);
  CHECK_EQ(show.env.played.size(), presents);  // nothing moved
  show.env.update.percent = 40;
  show.advance_s(1);
  CHECK_EQ(show.env.played.size(), presents + 1);
  // Nothing else takes the panel meanwhile: pairing, the auto-swap, a stream.
  core::makapix_state(p64::makapix::State::Pairing);
  CHECK(core::state().screen == core::Screen::Update);
  show.env.cfg->auto_swap_seconds = 5;
  show.advance_s(6);
  CHECK(core::state().screen == core::Screen::Update);
  core::stream_started();
  CHECK(!core::state().stage.stream_up());
  core::stream_ended();
  // The install fails: "failed" for 10 s, then the artwork again.
  show.env.update.phase = P::Failed;
  show.env.update.error = "SHA256 mismatch";
  show.advance_s(1);
  CHECK(core::state().screen == core::Screen::Update);
  CHECK(core::state().update_drawn.phase == P::Failed);
  show.advance_s(10);
  CHECK(core::state().screen == core::Screen::None);
  show.env.complete_loads();
  CHECK(show.current() == a);
  show.advance_s(1);
  CHECK(core::state().screen == core::Screen::None);  // the error that stays does not bring it back
}

TEST_CASE("show core: a written update stays on the panel until the reboot") {
  using P = p64::show::ShowEnv::UpdateState::Phase;
  Show show;
  show.env.update.phase = P::Verifying;
  show.advance_s(1);
  show.env.update.phase = P::Ready;
  show.advance_s(1);
  CHECK(core::state().update_drawn.phase == P::Ready);
  show.env.cfg->auto_swap_seconds = 5;
  show.advance_s(120);
  CHECK(core::state().screen == core::Screen::Update);
  core::next();  // Next ends the screen for a moment; the update puts it back
  show.advance_s(1);
  CHECK(core::state().screen == core::Screen::Update);
}

}  // namespace

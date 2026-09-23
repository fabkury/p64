// Host unit tests: the show's decision rules (main/show_rules.cpp) and the show core
// (main/show_core.cpp) driven through whole scenarios by a fake ShowEnv, with the bugs
// recorded in firmware/docs/PROGRESS.md replayed.
#include "common.hpp"
#include "show_core.hpp"
#include "show_rules.hpp"

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
  // A listing that landed empty is not a missing listing (the @Sendew channel of
  // 2026-09-22: an artist with no posts read "no listing yet" after a good refresh).
  mk.refreshed = true;
  CHECK(rules::channel_status(mk) == "no artworks");
  mk.oversized = 3;
  mk.max_side = 128;
  CHECK(rules::channel_status(mk) == "nothing fits 128 px (3 too large)");
  mk.online = false;
  CHECK(rules::channel_status(mk) == "offline");
  mk.refreshed = false;
  mk.oversized = 0;
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
  std::shared_ptr<FrameSource> widget(p64::system::WidgetKind kind) override {
    return std::make_shared<p64::playback::StaticSource>(std::string("widget:") + widget_name(kind), Frame());
  }
  const char *widget_name(p64::system::WidgetKind kind) override {
    return kind == p64::system::WidgetKind::Weather ? "weather"
           : kind == p64::system::WidgetKind::Temperature ? "temperature" : "clock";
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
  void makapix_set_active_channels(const std::vector<p64::makapix::ChannelRef> &) override {}
  bool makapix_snapshot(const p64::makapix::ChannelRef &, p64::makapix::ChannelSnapshot &) override { return false; }
  std::string makapix_artwork_path(const p64::content::MakapixEntry &) override { return ""; }
  void makapix_note_shown(int32_t, const p64::makapix::ChannelRef *, bool) override {}
  void makapix_note_hidden() override {}
  void makapix_note_load_failed(const p64::content::MakapixEntry &, bool) override {}
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
  show.env.cfg->clock.analogue = true;  // the face changed on the Settings page
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

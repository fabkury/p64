// The show core (show_core.hpp): the logic of the former show.cpp, moved unchanged apart
// from its outside calls, which go through ShowEnv, and its globals, which are the fields
// of one State (review of 2026-09-22, proposal P-T1). No ESP-IDF include.
#include "show_core.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>

#include "p64/content/local_index.hpp"
#include "p64/content/makapix_index.hpp"
#include "p64/content/playset_json.hpp"
#include "p64/decode/decoder.hpp"
#include "status_screens.hpp"

namespace p64::show::core {
namespace {

constexpr int64_t kSecond = 1000000;
constexpr int64_t kRescanDebounceUs = 2 * kSecond;   // after file manager changes
constexpr int64_t kRetryUs = 5 * kSecond;            // while nothing can be shown
constexpr int64_t kIdleRescanUs = 30 * kSecond;      // rescan period while nothing can be shown
constexpr int64_t kPairedScreenUs = 10 * kSecond;    // spec 6.4
constexpr int64_t kUpdateFailedScreenUs = 10 * kSecond;  // "Update failed", then the artwork
constexpr uint32_t kMaxPrepareFailures = 20;         // consecutive load failures before giving up on a pick

ShowEnv *g_env = nullptr;
gfx::Frame *g_scratch = nullptr;  // the status screens are drawn here (PSRAM on the device)
State st;
std::atomic<system::MainState> g_main_state{system::MainState::AnimationShow};  // read by the overlay hook
std::atomic<bool> g_artwork_up{false};  // read by the overlay hook on the player task
std::atomic<bool> g_stream_up{false};   // mirrors st.stage.stream_up() for the overlay hook

void log(char level, const char *format, ...) __attribute__((format(printf, 2, 3)));
void log(char level, const char *format, ...) {
  va_list args;
  va_start(args, format);
  g_env->vlog(level, format, args);
  va_end(args);
}
#define LOGE(...) log('E', __VA_ARGS__)
#define LOGW(...) log('W', __VA_ARGS__)
#define LOGI(...) log('I', __VA_ARGS__)
#define LOGD(...) log('D', __VA_ARGS__)

int64_t now_us() { return g_env->now_us(); }
system::Settings settings() { return *g_env->settings(); }

// Every source the show puts up goes through here. While a stream holds the panel the
// source is kept aside instead (the state keeps running invisibly, spec 8.3) and goes up
// when the stream ends.
void present(std::shared_ptr<playback::FrameSource> src) {
  if (st.stage.present(src)) g_env->play(std::move(src));
}

std::string relative_path(const std::string &absolute) {
  const std::string prefix = g_env->storage_root() + "/";
  return absolute.rfind(prefix, 0) == 0 ? absolute.substr(prefix.size()) : absolute;
}

std::string basename_of(const std::string &path) { return path.substr(path.rfind('/') + 1); }

std::string channel_dir(const content::ChannelSpec &spec) {
  return spec.identifier.empty() ? g_env->animations_dir() : g_env->animations_dir() + "/" + spec.identifier;
}

makapix::ChannelRef ref_of(const content::ChannelSpec &spec) { return makapix::ChannelRef{spec.kind, spec.identifier}; }

std::string display_name(const content::ChannelSpec &spec) {
  return spec.display_name.empty() ? spec.default_display_name() : spec.display_name;
}

// Why a channel cannot supply artworks right now ("" when it can).
std::string channel_status(const ChannelRuntime &ch) {
  rules::ChannelFacts f;
  f.supported = ch.spec.supported();
  f.local = ch.spec.kind == content::ChannelKind::Local;
  if (f.local) {
    f.card_mounted = g_env->card_mounted();
  } else {
    const makapix::Status ms = g_env->makapix_status();
    f.needs_pairing = ch.spec.needs_pairing();
    f.paired = ms.state == makapix::State::Paired;
    f.online = ms.online;
    f.index_entries = ch.mk_entries.size();
    f.cached = ch.mk_cached.size();
    f.refreshed = ch.mk_last_refresh != 0;
    f.oversized = ch.mk_oversized;
    f.max_side = settings().makapix_max_side;
  }
  return rules::channel_status(f);
}

// Why nothing can be shown: the first reason among the channels, "empty" when a usable
// channel simply has no files.
std::string no_artwork_reason() {
  std::vector<rules::ChannelSummary> summary;
  summary.reserve(st.channels.size());
  for (const ChannelRuntime &ch : st.channels) summary.push_back({ch.status, ch.available});
  return rules::no_artwork_reason(summary);
}

bool can_swap_now() { return now_us() >= st.boot_until_us && st.screen == Screen::None; }

// Whether the Animation show (spec 6.1) runs: artworks are picked, the timer swaps.
bool show_active() { return g_main_state == system::MainState::AnimationShow; }

bool stream_allowed();

void release_stream();

// Any request for an artwork (a playset, next, previous, history, play-this, a Makapix
// command, a tap on the shell) means the user wants the show: the device leaves the
// Widget or Stream state and the choice is persisted, so the Settings page and the next
// boot agree with the panel (decided with the user on 2026-09-21, after the device was
// found with an artwork frozen on the panel in Widget state: the artwork paths never
// looked at the main state while the swap timer did).
void enter_animation_show(const char *why) {
  if (show_active()) return;
  LOGI("main state: animation show (%s)", why);
  g_main_state = system::MainState::AnimationShow;
  st.want_widget = false;
  st.swap_at_us = now_us();  // a widget still up while the artwork loads gets a full interval
  g_env->persist_main_state(system::MainState::AnimationShow);
  if (g_stream_up && !stream_allowed()) release_stream();
}

bool has_local_channels() {
  for (const ChannelRuntime &ch : st.channels) {
    if (!ch.makapix) return true;
  }
  return false;
}

void apply_scheduler_modes(const system::Settings &s) {
  st.scheduler.set_pick_mode(s.pick_mode == system::PickMode::Recency ? content::PickMode::Recency
                                                                      : content::PickMode::Random);
  st.scheduler.set_channel_select(s.channel_select == system::ChannelSelect::Swrr ? content::ChannelSelect::Swrr
                                                                                  : content::ChannelSelect::Stochastic);
}

void update_counts() {
  for (size_t i = 0; i < st.channels.size(); ++i) {
    ChannelRuntime &ch = st.channels[i];
    ch.status = channel_status(ch);
    const uint32_t count = ch.makapix ? static_cast<uint32_t>(ch.mk_cached.size())
                                      : (ch.available ? static_cast<uint32_t>(ch.entries.size()) : 0);
    st.scheduler.set_count(i, ch.status.empty() ? count : 0);
  }
}

// Pulls the Makapix channels' indexes from the Makapix component.
void snapshot_makapix(ChannelRuntime &ch) {
  makapix::ChannelSnapshot snap;
  ch.mk_entries.clear();
  ch.mk_cached.clear();
  ch.mk_last_refresh = 0;
  ch.mk_oversized = 0;
  if (g_env->makapix_snapshot(ref_of(ch.spec), snap)) {
    ch.mk_entries = std::move(snap.entries);
    ch.mk_last_refresh = snap.last_refresh;
    ch.mk_oversized = snap.oversized;
    // Pickable: cached and within the size limit (an index walked before the limit was
    // lowered still lists bigger artworks until its refresh lands).
    const uint16_t max_side = settings().makapix_max_side;
    for (size_t i = 0; i < ch.mk_entries.size() && i < 65535; ++i) {
      const content::MakapixEntry &e = ch.mk_entries[i];
      if ((e.flags & content::kMakapixCached) && content::fits_side(e, max_side)) ch.mk_cached.push_back(static_cast<uint16_t>(i));
    }
  }
  ch.available = static_cast<uint32_t>(ch.mk_cached.size());
}

// --- picking and playing --------------------------------------------------------------

bool pick_fresh(Pick &out) {
  uint32_t attempts = st.scheduler.available_channels();
  const content::HistoryItem *cur = st.history.current();
  while (attempts-- > 0) {
    const int c = st.scheduler.select_channel();
    if (c < 0 || static_cast<size_t>(c) >= st.channels.size()) return false;
    ChannelRuntime &ch = st.channels[c];
    const int avoid = rules::avoid_entry(cur != nullptr, cur ? cur->channel_index : -1, cur ? cur->playset : std::string(),
                                         cur ? cur->entry_index : -1, c, st.playset.name);
    const size_t pool = ch.makapix ? ch.mk_cached.size() : ch.entries.size();
    const size_t tries = std::min<size_t>(pool, 16);
    for (size_t t = 0; t < tries; ++t) {
      const int e = st.scheduler.pick_entry(c, avoid);
      if (e < 0 || static_cast<size_t>(e) >= pool) break;
      out.generation = st.generation;
      out.channel = c;
      out.entry = e;
      out.pool = static_cast<uint32_t>(pool);
      out.channel_name = display_name(ch.spec);
      if (ch.makapix) {
        const content::MakapixEntry &entry = ch.mk_entries[ch.mk_cached[e]];
        out.makapix = true;
        out.mk_entry = entry;
        out.path = g_env->makapix_artwork_path(entry);
        out.name = entry.sqid[0] ? std::string(entry.sqid) : "post " + std::to_string(entry.post_id);
      } else {
        const content::LocalEntry &entry = ch.entries[e];
        if (entry.missing || entry.rejected) continue;
        out.makapix = false;
        out.path = channel_dir(ch.spec) + "/" + entry.name;
        out.name = entry.name;
      }
      return true;
    }
  }
  return false;
}

void mark_entry(const Pick &pick, bool missing) {
  if (pick.generation != st.generation || pick.channel < 0 || static_cast<size_t>(pick.channel) >= st.channels.size()) return;
  ChannelRuntime &ch = st.channels[pick.channel];
  if (pick.makapix) {
    g_env->makapix_note_load_failed(pick.mk_entry, missing);
    snapshot_makapix(ch);
    update_counts();
    return;
  }
  if (pick.entry < 0 || static_cast<size_t>(pick.entry) >= ch.entries.size()) return;
  content::LocalEntry &entry = ch.entries[pick.entry];
  if (pick.name != entry.name || entry.missing || entry.rejected) return;
  if (missing) {
    entry.missing = 1;
  } else {
    entry.rejected = 1;
  }
  if (ch.available > 0) --ch.available;
  if (ch.available == 0) st.scheduler.set_count(pick.channel, 0);
}

void request_prepare() {
  if (st.prepared || st.prepared_load_id) return;
  Pick p;
  if (!pick_fresh(p)) return;
  st.prepared_pick = p;
  st.prepared_load_id = g_env->load(p.path, settings().background);
}

void report_shown(const content::HistoryItem &item) {
  if (item.post_id < 0) {
    g_env->makapix_note_hidden();
    return;
  }
  makapix::ChannelRef ref{static_cast<content::ChannelKind>(item.channel_kind), item.channel_identifier};
  const bool from_channel = item.source == content::Source::Channel;
  g_env->makapix_note_shown(item.post_id, from_channel ? &ref : nullptr, item.source == content::Source::PlayThisMakapix);
}

void play_artwork(std::shared_ptr<playback::Artwork> art, content::HistoryItem item, bool push) {
  const int64_t now = now_us();
  present(art);
  st.current = std::move(art);
  st.widget.reset();
  st.widget_up = false;
  g_artwork_up = true;
  st.status_reason.clear();
  st.screen = Screen::None;
  st.paused = false;
  st.swap_at_us = now;
  item.shown_at_us = now;
  if (push) {
    st.history.push(std::move(item));
  } else if (content::HistoryItem *cur = st.history.current()) {
    cur->shown_at_us = now;
  }
  ++st.swaps;
  const decode::Info &info = st.current->info();
  const content::HistoryItem *cur = st.history.current();
  LOGI("playing %s (%s %dx%d, %u bytes%s) from %s, history %u/%u", st.current->name().c_str(),
           decode::format_name(st.current->format()), info.width, info.height,
           static_cast<unsigned>(st.current->file_bytes()), info.animated ? "" : ", static",
           cur ? (cur->channel.empty() ? "play-this" : cur->channel.c_str()) : "?",
           static_cast<unsigned>(st.history.position() + 1), static_cast<unsigned>(st.history.size()));
  if (cur) report_shown(*cur);
  g_env->playback_swapped(static_cast<int32_t>(st.history.position()));
}

void play_prepared() {
  content::HistoryItem item;
  item.kind = content::ItemKind::Artwork;
  item.source = content::Source::Channel;
  item.path = st.prepared_pick.path;
  item.name = st.prepared_pick.name;
  item.channel = st.prepared_pick.channel_name;
  item.channel_index = st.prepared_pick.channel;
  item.entry_index = st.prepared_pick.entry;
  item.playset = st.playset.name;
  if (st.prepared_pick.channel >= 0 && static_cast<size_t>(st.prepared_pick.channel) < st.channels.size()) {
    const content::ChannelSpec &spec = st.channels[st.prepared_pick.channel].spec;
    item.channel_kind = static_cast<uint8_t>(spec.kind);
    item.channel_identifier = spec.identifier;
  }
  if (st.prepared_pick.makapix) {
    item.post_id = st.prepared_pick.mk_entry.post_id;
    item.sqid = st.prepared_pick.mk_entry.sqid;
  }
  play_artwork(std::move(st.prepared), std::move(item), true);
  st.prepared.reset();
  st.want_prepared_now = false;
  request_prepare();
}

void show_frame(const char *name) {
  present(g_env->static_frame(name, *g_scratch));
  st.setup_pages_up = false;
  st.current.reset();
  st.widget.reset();
  st.widget_up = false;
  g_artwork_up = false;
  st.paused = false;
  g_env->makapix_note_hidden();
  g_env->playback_swapped(-1);
}

// The setup pages (spec 6.4): the AP to join and the address to open, a page every 3 s.
void draw_setup_page() {
  const net::wifi::Status w = g_env->wifi_status();
  status_screens::setup_page(*g_scratch, st.setup_page, w.ap_ssid.empty() ? "p64-setup" : w.ap_ssid,
                             w.ap_ip.empty() ? "192.168.4.1" : w.ap_ip);
  st.setup_page_at_us = now_us() + status_screens::kSetupPageUs;
}

bool setup_instead_of_no_artwork() {
  const net::wifi::Status w = g_env->wifi_status();
  return rules::setup_screen(w.setup_mode, w.network_saved) == rules::SetupScreen::InsteadOfNoArtwork;
}

void show_status(const std::string &reason) {
  if (st.screen != Screen::None) return;  // a screen with a purpose holds the panel
  if (!st.current && !st.paused && st.status_reason == reason) return;
  st.status_reason = reason;
  st.retry_at_us = now_us() + kRetryUs;
  if (!st.rescan_due_us && has_local_channels()) st.rescan_due_us = now_us() + kIdleRescanUs;
  LOGW("no artwork: %s", reason.c_str());
  if (setup_instead_of_no_artwork()) {  // nothing to play and no network: say how to set it up
    st.setup_page = 0;
    draw_setup_page();
    show_frame("setup");
    st.setup_pages_up = true;
    return;
  }
  status_screens::no_artwork(*g_scratch, reason);
  show_frame("no artwork");
}

status_screens::UpdateView update_view(const ShowEnv::UpdateState &u) {
  using P = ShowEnv::UpdateState::Phase;
  using V = status_screens::UpdateView::Phase;
  status_screens::UpdateView v;
  v.phase = u.phase == P::Verifying ? V::Verifying : u.phase == P::Ready ? V::Ready : u.phase == P::Failed ? V::Failed : V::Downloading;
  v.version = u.version;
  v.percent = u.percent;
  v.error = u.error;
  return v;
}

void show_screen(Screen screen, int64_t for_us) {
  if (st.screen == Screen::Update && screen != Screen::Update) return;  // the update holds the panel to the end
  switch (screen) {
    case Screen::Pairing: status_screens::pairing_code(*g_scratch, g_env->makapix_status().code); break;
    case Screen::Paired: status_screens::paired(*g_scratch); break;
    case Screen::Connected: {
      const net::wifi::Status w = g_env->wifi_status();
      status_screens::connected(*g_scratch, w.hostname, w.ip);
      break;
    }
    case Screen::Setup: draw_setup_page(); break;
    case Screen::Update: status_screens::update(*g_scratch, update_view(st.update_drawn)); break;
    case Screen::None: return;
  }
  st.screen = screen;
  st.screen_until_us = for_us ? now_us() + for_us : 0;
  st.status_reason.clear();
  show_frame(screen == Screen::Setup ? "setup" : screen == Screen::Update ? "update" : "status");
  st.setup_pages_up = screen == Screen::Setup;
}

void load_current_history_item(Pending::Purpose purpose, int direction);

// A widget takes the panel: the Widget state, an interlude, or an interlude revisited
// through history (spec 6.1, 6.2).
void play_widget(system::WidgetKind kind, bool interlude) {
  st.widget = g_env->widget(kind);
  st.widget_kind = kind;
  present(st.widget);
  st.current.reset();
  g_artwork_up = false;
  st.widget_up = true;
  st.status_reason.clear();
  st.screen = Screen::None;
  st.paused = false;
  st.swap_at_us = now_us();
  st.want_widget = false;
  if (interlude) {
    content::HistoryItem item;
    item.kind = content::ItemKind::Interlude;
    item.source = content::Source::Channel;
    item.name = g_env->widget_name(kind);
    item.playset = st.playset.name;
    item.widget = static_cast<uint8_t>(kind);
    item.shown_at_us = st.swap_at_us;
    st.history.push(std::move(item));
  }
  g_env->makapix_note_hidden();
  LOGI("widget %s%s", g_env->widget_name(kind), interlude ? " (interlude)" : "");
  g_env->playback_swapped(static_cast<int32_t>(st.history.position()));
}

// At an auto-swap, each widget with an interlude probability is rolled in a fixed order;
// the first that wins takes the slot (spec 6.1).
bool roll_interlude() {
  const system::Settings s = settings();
  const uint8_t percent[3] = {s.interlude_clock, s.interlude_weather, s.interlude_temperature};
  const system::WidgetKind kinds[3] = {system::WidgetKind::Clock, system::WidgetKind::Weather,
                                       system::WidgetKind::Temperature};
  const int winner = rules::roll_interlude(percent, [] { return g_env->random(); });
  if (winner < 0) return false;
  play_widget(kinds[winner], true);
  return true;
}

void swap_fresh();

void show_stream_waiting() {
  const net::wifi::Status w = g_env->wifi_status();
  const system::Settings s = settings();
  status_screens::stream_waiting(*g_scratch, w.hostname, w.connected ? w.ip : "no network", s.ddp_port, s.raw_udp_port);
  st.status_reason.clear();
  show_frame("stream waiting");
}

bool stream_allowed() {
  return rules::stream_allowed(g_main_state == system::MainState::Stream, settings().stream_takeover);
}

// The first complete frame of a stream takes the panel (spec 8.3). The boot animation
// and a screen that needs the user (pairing) finish first; informational screens are
// simply covered and their timers run on.
void take_stream() {
  const rules::StreamGate gate = rules::stream_gate(st.stream_active, st.stage.stream_up(), stream_allowed(),
                                                     now_us() < st.boot_until_us,
                                                     st.screen == Screen::Pairing || st.screen == Screen::Setup ||
                                                         st.screen == Screen::Update);
  if (gate == rules::StreamGate::No) return;
  if (gate == rules::StreamGate::Wait) {
    st.want_stream = true;
    return;
  }
  st.want_stream = false;
  st.stage.take(g_env->stream_source());
  g_stream_up = true;
  g_env->play(st.stage.on_panel());
  g_env->makapix_note_hidden();
  const stream::Status ss = g_env->stream_status();
  LOGI("stream takes the panel: %s %dx%d from %s (behind: %s)", ss.protocol.c_str(), ss.width, ss.height,
           ss.sender.c_str(), st.stage.behind() ? st.stage.behind()->name().c_str() : "nothing");
  g_env->playback_swapped(-1);
}

// The stream ended (silence) or may no longer hold the panel: back to what the state
// has up, which kept running meanwhile.
void release_stream() {
  st.want_stream = false;
  if (!st.stage.stream_up()) return;
  std::shared_ptr<playback::FrameSource> back = st.stage.release();
  g_stream_up = false;
  g_env->stream_wake();
  if (back) {
    present(back);
  } else if (g_main_state == system::MainState::Stream) {
    show_stream_waiting();
  } else {
    swap_fresh();
  }
  if (st.current) {
    if (const content::HistoryItem *cur = st.history.current()) report_shown(*cur);
  }
  LOGI("stream released the panel: back to %s", st.stage.on_panel() ? st.stage.on_panel()->name().c_str() : "?");
  g_env->playback_swapped(st.current ? static_cast<int32_t>(st.history.position()) : -1);
}

// A fresh pick goes up now (auto-swap, next at the end of history, activation). When
// nothing is ready, whatever is on the panel stays until something is (spec 4.5).
void swap_fresh() {
  if (st.prepared) {
    if (can_swap_now()) {
      play_prepared();
    } else {
      st.want_prepared_now = true;
    }
    return;
  }
  if (st.prepared_load_id) {
    st.want_prepared_now = true;
    return;
  }
  Pick p;
  if (pick_fresh(p)) {
    st.prepared_pick = p;
    st.prepared_load_id = g_env->load(p.path, settings().background);
    st.want_prepared_now = true;
    return;
  }
  if (!st.current) {
    show_status(no_artwork_reason());
  } else if (!st.rescan_due_us && has_local_channels()) {
    st.rescan_due_us = now_us() + kRetryUs;  // the index may be stale; look again soon
  }
}

// Ends a timed status screen: back to the artwork that was up, or a fresh one.
void end_screen() {
  st.screen = Screen::None;
  st.screen_until_us = 0;
  if (st.history.current() && !st.paused) {
    load_current_history_item(Pending::Purpose::Resume, 0);
  } else {
    swap_fresh();
  }
}

void load_current_history_item(Pending::Purpose purpose, int direction) {
  const content::HistoryItem *item = st.history.current();
  if (!item) return;
  if (item->kind == content::ItemKind::Interlude) {
    play_widget(static_cast<system::WidgetKind>(item->widget), false);
    if (content::HistoryItem *cur = st.history.current()) cur->shown_at_us = now_us();
    return;
  }
  st.pending.purpose = purpose;
  st.pending.direction = direction;
  st.pending.item = *item;
  st.pending.load_id = g_env->load(item->path, settings().background);
}

// --- scans and playsets ---------------------------------------------------------------

void start_scan(const content::Playset &playset, bool activation) {
  ++st.generation;
  st.scan_running = true;
  if (activation) st.activate_after_scan = true;
  g_env->scan(st.generation, playset);
}

bool same_channels(const content::Playset &a, const content::Playset &b) {
  if (a.name != b.name || a.channels.size() != b.channels.size()) return false;
  for (size_t i = 0; i < a.channels.size(); ++i) {
    const content::ChannelSpec &x = a.channels[i], &y = b.channels[i];
    if (x.kind != y.kind || x.identifier != y.identifier || x.weight != y.weight || x.offset != y.offset) return false;
  }
  return true;
}

void install(loader::ScanResult &r) {
  const bool fresh = st.activate_after_scan || !same_channels(st.playset, r.playset) ||
                     st.channels.size() != r.playset.channels.size();
  st.playset = r.playset;
  std::vector<ChannelRuntime> channels(st.playset.channels.size());
  std::vector<makapix::ChannelRef> refs;
  size_t entries = 0;
  for (size_t i = 0; i < channels.size(); ++i) {
    ChannelRuntime &ch = channels[i];
    ch.spec = st.playset.channels[i];
    ch.makapix = ch.spec.is_makapix();
    if (ch.makapix) {
      refs.push_back(ref_of(ch.spec));
    } else {
      if (i < r.entries.size()) ch.entries = std::move(r.entries[i]);
      ch.available = static_cast<uint32_t>(ch.entries.size());
      if (i < r.errors.size() && !r.errors[i].empty() && g_env->card_mounted()) ch.status = r.errors[i];
    }
    entries += ch.entries.size();
  }
  st.channels = std::move(channels);
  g_env->makapix_set_active_channels(refs);
  for (ChannelRuntime &ch : st.channels) {
    if (ch.makapix) snapshot_makapix(ch);
  }
  if (fresh) {
    std::vector<uint32_t> weights, offsets;
    for (const content::ChannelSpec &c : st.playset.channels) {
      weights.push_back(c.weight);
      offsets.push_back(c.offset);
    }
    const uint64_t seed = (static_cast<uint64_t>(g_env->random()) << 32) | g_env->random();
    st.scheduler.configure(weights, offsets, seed);
  }
  apply_scheduler_modes(settings());
  update_counts();
  ++st.channels_version;
  g_env->notify_web();
  st.last_scan_ms = r.took_ms;
  LOGI("playset %s: %u channels, %u local entries%s, scan %u ms%s", st.playset.name.c_str(),
           static_cast<unsigned>(st.channels.size()), static_cast<unsigned>(entries),
           r.skipped ? (", " + std::to_string(r.skipped) + " skipped").c_str() : "", static_cast<unsigned>(r.took_ms), fresh ? " (fresh)" : "");
  if (fresh) {
    // The prepared artwork was picked against the old playset or index: drop it.
    st.prepared.reset();
    st.prepared_load_id = 0;
    st.prepared_pick = Pick{};
    st.want_prepared_now = false;
  }
  if (st.activate_after_scan) {
    st.activate_after_scan = false;
    if (show_active()) {
      swap_fresh();
    } else {
      request_prepare();  // the boot restore in Widget or Stream state: ready for later
    }
  } else if (!st.current && !st.paused && !st.widget_up && show_active()) {
    swap_fresh();
  } else {
    request_prepare();
  }
}

bool resolve_playset(const std::string &name, content::Playset &out, std::string &error) {
  content::Builtin b;
  if (content::builtin_from_name(name, b)) {
    out = content::builtin_playset(b, {});
    return true;
  }
  return g_env->load_playset(name, out, error);
}

void activate_playset(const std::string &name, bool persist) {
  content::Builtin b;
  if (content::builtin_from_name(name, b) && b == content::Builtin::Followed) {
    // The server generates this one; the Makapix component hands it back through
    // activate_transient() when it lands.
    std::string error;
    if (!g_env->makapix_play_followed(error)) {
      LOGW("cannot activate Followed: %s", error.c_str());
      st.last_error = "Followed: " + error;
      return;
    }
    if (persist) g_env->persist_active_playset(name);
    return;
  }
  content::Playset p;
  std::string error;
  if (!resolve_playset(name, p, error)) {
    LOGW("cannot activate %s: %s", name.c_str(), error.c_str());
    st.last_error = "playset " + name + ": " + error;
    return;
  }
  LOGI("activating playset %s", name.c_str());
  if (persist) g_env->persist_active_playset(p.name);
  st.playset.name = p.name;  // status shows the new name while the scan runs
  st.playset.builtin = p.builtin;
  start_scan(p, true);
}

void do_activate_transient(const content::Playset &p) {
  LOGI("activating playset %s (%u channels, from the site)", p.name.c_str(),
           static_cast<unsigned>(p.channels.size()));
  st.playset.name = p.name;
  st.playset.builtin = p.builtin;
  start_scan(p, true);
}

// --- command handling -----------------------------------------------------------------

void on_loaded_impl(std::unique_ptr<loader::LoadResult> res) {
  if (res->id == st.prepared_load_id) {
    st.prepared_load_id = 0;
    if (!res->artwork) {
      LOGW("%s: %s", res->path.c_str(), res->error.c_str());
      ++st.load_failures;
      st.last_error = basename_of(res->path) + ": " + res->error;
      mark_entry(st.prepared_pick, res->missing);
      if (++st.prepare_failures < kMaxPrepareFailures) {
        request_prepare();
        if (st.want_prepared_now && st.prepared_load_id) return;
      }
      if (!st.current && !st.paused) show_status(no_artwork_reason());
      return;
    }
    st.prepare_failures = 0;
    st.prepared = res->artwork;
    LOGD("prepared %s (read %u ms, open %u ms)", res->artwork->name().c_str(), static_cast<unsigned>(res->read_ms), static_cast<unsigned>(res->open_ms));
    if (st.want_prepared_now && can_swap_now() && show_active()) play_prepared();
    return;
  }
  if (res->id == st.pending.load_id) {
    st.pending.load_id = 0;
    const Pending::Purpose purpose = st.pending.purpose;
    st.pending.purpose = Pending::Purpose::None;
    if (!res->artwork) {
      LOGW("%s: %s", res->path.c_str(), res->error.c_str());
      ++st.load_failures;
      st.last_error = basename_of(res->path) + ": " + res->error;
      if (purpose == Pending::Purpose::PlayThis) return;  // the current picture stays (spec 4.5)
      // A history item whose file is gone is dropped and the walk continues (spec 4.6).
      const size_t pos = st.history.position();
      if (st.history.size() && st.history.at(pos).path == res->path) st.history.remove(pos);
      if (purpose == Pending::Purpose::Navigate && st.pending.direction < 0 && st.history.can_back()) {
        st.history.back();
        load_current_history_item(purpose, -1);
      } else if (purpose == Pending::Purpose::Navigate && st.pending.direction > 0 && st.history.can_forward()) {
        st.history.forward();
        load_current_history_item(purpose, 1);
      } else if (!st.current || purpose == Pending::Purpose::Resume || st.pending.direction > 0) {
        swap_fresh();
      }
      return;
    }
    if (purpose == Pending::Purpose::Resume) {
      const content::HistoryItem *cur = st.history.current();
      if (st.current || !cur || cur->path != st.pending.item.path) {
        LOGD("stale resume of %s ignored", res->path.c_str());
        return;  // something else went up meanwhile
      }
    }
    play_artwork(res->artwork, st.pending.item, purpose == Pending::Purpose::PlayThis);
    return;
  }
  // A result for a request that was superseded (playset changed, newer navigation).
  LOGD("stale load result for %s ignored", res->path.c_str());
}

void on_scanned_impl(std::unique_ptr<loader::ScanResult> res) {
  st.scan_running = false;
  if (res->generation != st.generation) {
    LOGD("stale scan result ignored");
  } else {
    install(*res);
  }
  if (st.scan_again) {
    st.scan_again = false;
    st.rescan_due_us = now_us();
  }
}

void on_makapix_changed() {
  bool any = false;
  for (ChannelRuntime &ch : st.channels) {
    if (!ch.makapix) continue;
    snapshot_makapix(ch);
    any = true;
  }
  if (!any) return;
  update_counts();
  ++st.channels_version;
  g_env->notify_web();
  // A pick prepared while the cache was still tiny (the same artwork again, or one of
  // a handful) is replaced once there is something to choose from.
  if (st.prepared && st.prepared_pick.makapix && st.prepared_pick.channel >= 0 &&
      static_cast<size_t>(st.prepared_pick.channel) < st.channels.size()) {
    const uint32_t pool = static_cast<uint32_t>(st.channels[st.prepared_pick.channel].mk_cached.size());
    const content::HistoryItem *cur = st.history.current();
    if (rules::replace_prepared_pick(st.prepared_pick.mk_entry.post_id, st.prepared_pick.pool, pool,
                                     cur && st.current, cur ? cur->post_id : -1)) {
      st.prepared.reset();
      st.prepared_pick = Pick{};
    }
  }
  if (!st.current && !st.widget_up && !st.paused && st.screen == Screen::None) {
    swap_fresh();
  } else if (!st.prepared && !st.prepared_load_id) {
    request_prepare();
  }
}

void on_makapix_state(uint32_t state) {
  const auto s = static_cast<makapix::State>(state);
  if (s == makapix::State::Pairing) {
    show_screen(Screen::Pairing, 0);
  } else if (st.screen == Screen::Pairing) {
    if (s == makapix::State::Paired) {
      show_screen(Screen::Paired, kPairedScreenUs);
    } else {
      end_screen();
    }
  }
  update_counts();
  if (s == makapix::State::Paired) st.rescan_due_us = now_us();  // channels that needed pairing can refresh
}

void do_next() {
  enter_animation_show("next");
  if (st.paused) st.paused = false;
  if (st.screen != Screen::None) end_screen();
  if (st.history.can_forward()) {
    st.history.forward();
    load_current_history_item(Pending::Purpose::Navigate, 1);
    return;
  }
  swap_fresh();
}

void do_previous() {
  if (st.paused) st.paused = false;
  if (!st.history.can_back()) {
    LOGI("previous: at the start of history");
    return;
  }
  enter_animation_show("previous");
  st.history.back();
  load_current_history_item(Pending::Purpose::Navigate, -1);
}

void do_go_to(size_t index) {
  if (!st.history.go_to(index)) return;
  enter_animation_show("history");
  if (st.paused) st.paused = false;
  load_current_history_item(Pending::Purpose::Navigate, 0);
}

void do_pause() {
  if (st.paused) return;
  status_screens::black(*g_scratch);
  present(g_env->static_frame("paused", *g_scratch));
  st.current.reset();
  st.widget.reset();
  st.widget_up = false;
  g_artwork_up = false;
  st.paused = true;
  g_env->makapix_note_hidden();
  LOGI("paused");
  g_env->playback_swapped(-1);
}

void do_resume() {
  if (!st.paused) return;
  enter_animation_show("resume");
  if (st.history.current()) {
    load_current_history_item(Pending::Purpose::Resume, 0);
  } else {
    st.paused = false;
    swap_fresh();
  }
}

void do_play_file(const std::string &path, int32_t post_id, const std::string &name) {
  enter_animation_show("play-this");
  content::HistoryItem item;
  item.kind = content::ItemKind::Artwork;
  item.source = post_id >= 0 ? content::Source::PlayThisMakapix
                             : (path.rfind(g_env->downloads_dir(), 0) == 0 || path.rfind("mem:dl-", 0) == 0)
                                   ? content::Source::PlayThisUrl
                                   : content::Source::PlayThisFile;
  item.path = path;
  item.name = name.empty() ? basename_of(path) : name;
  item.playset = st.playset.name;
  item.post_id = post_id;
  st.pending.purpose = Pending::Purpose::PlayThis;
  st.pending.direction = 0;
  st.pending.item = std::move(item);
  st.pending.load_id = g_env->load(path, settings().background);
}

// The auto-swap timer runs while an artwork is up, whatever the state says (an artwork
// on the panel must never freeze), and while a widget is up inside the show (an
// interlude). The Widget state's own widget stays indefinitely (spec 6.2).
bool swap_timer_runs() {
  return rules::swap_timer_runs(st.paused, static_cast<bool>(st.current), st.widget_up, show_active());
}

// The Update screen follows the firmware update: progress while it downloads and verifies,
// "ready" until the reboot; an install that fails shows "failed" for a while. A failed
// release check never had the screen up, so it shows nothing.
void follow_update() {
  using P = ShowEnv::UpdateState::Phase;
  const ShowEnv::UpdateState u = g_env->update_state();
  if (u.phase == P::Downloading || u.phase == P::Verifying || u.phase == P::Ready) {
    const bool changed = u.phase != st.update_drawn.phase || u.percent != st.update_drawn.percent ||
                         u.version != st.update_drawn.version;
    if (st.screen != Screen::Update || changed) {
      if (st.screen != Screen::Update) LOGI("update screen: %s", u.version.c_str());
      st.update_drawn = u;
      show_screen(Screen::Update, 0);
    }
  } else if (st.screen == Screen::Update && st.screen_until_us == 0) {
    if (u.phase == P::Failed) {
      st.update_drawn = u;
      show_screen(Screen::Update, kUpdateFailedScreenUs);
    } else {
      end_screen();
    }
  }
}

// The setup pages follow setup mode (rules::setup_screen) and turn every 3 s.
void follow_setup() {
  const net::wifi::Status w = g_env->wifi_status();
  const rules::SetupScreen want = rules::setup_screen(w.setup_mode, w.network_saved);
  const int64_t now = now_us();
  if (want == rules::SetupScreen::Holds) {
    if (now < st.boot_until_us) return;  // the boot animation finishes first
    if (st.screen == Screen::None || st.screen == Screen::Connected) {
      st.setup_page = 0;
      show_screen(Screen::Setup, 0);
    } else if (st.screen == Screen::Setup && now >= st.setup_page_at_us) {
      ++st.setup_page;
      show_screen(Screen::Setup, 0);
    }
    return;
  }
  if (st.screen == Screen::Setup) {  // setup mode ended, or a network is saved now
    end_screen();
    return;
  }
  const bool no_artwork_up = !st.current && !st.widget_up && !st.paused && st.screen == Screen::None &&
                             !st.status_reason.empty() && !g_stream_up;
  if (!no_artwork_up) return;
  if (want == rules::SetupScreen::InsteadOfNoArtwork) {
    if (!st.setup_pages_up || now >= st.setup_page_at_us) {
      st.setup_page = st.setup_pages_up ? st.setup_page + 1 : 0;
      draw_setup_page();
      show_frame("setup");
      st.setup_pages_up = true;
    }
  } else if (st.setup_pages_up) {  // setup mode ended with nothing to play: the reason again
    status_screens::no_artwork(*g_scratch, st.status_reason);
    show_frame("no artwork");
  }
}

// Periodic work: the auto-swap timer, the boot hold, rescans, retries.
void tick_impl() {
  const int64_t now = now_us();
  const system::Settings s = settings();
  if (st.screen != Screen::None && st.screen_until_us && now >= st.screen_until_us) end_screen();
  follow_update();
  follow_setup();
  if (st.want_stream) take_stream();
  if (st.want_widget && now >= st.boot_until_us) play_widget(s.widget, false);
  if (st.want_prepared_now && st.prepared && can_swap_now() && show_active()) play_prepared();
  if (rules::auto_swap_due(swap_timer_runs(), s.auto_swap_seconds, now, st.swap_at_us)) {
    if (!roll_interlude()) swap_fresh();
  }
  if (st.rescan_due_us && now >= st.rescan_due_us) {
    if (st.scan_running) {
      st.scan_again = true;
    } else {
      start_scan(st.playset, false);
    }
    st.rescan_due_us = 0;
  }
  if (show_active() && !st.current && !st.widget_up && !st.paused && st.screen == Screen::None && !st.status_reason.empty() &&
      now >= st.retry_at_us) {
    st.retry_at_us = now + kRetryUs;
    swap_fresh();
    if (!st.current && !st.rescan_due_us && !st.scan_running && has_local_channels()) st.rescan_due_us = now + kIdleRescanUs;
  }
  if (st.screen == Screen::Connected && st.screen_until_us == 0) end_screen();
}

// How long the loop may sleep before tick() has something to do.
int64_t wait_us_impl() {
  const int64_t now = now_us();
  int64_t wait = kSecond;  // settings changes and the like are noticed within a second
  auto consider = [&](int64_t at) {
    if (at > 0) wait = std::min(wait, std::max<int64_t>(at - now, 0));
  };
  const system::Settings s = settings();
  if (swap_timer_runs() && s.auto_swap_seconds > 0) {
    consider(st.swap_at_us + static_cast<int64_t>(s.auto_swap_seconds) * kSecond);
  }
  if (st.want_widget) consider(st.boot_until_us);
  if (st.want_prepared_now && st.prepared) consider(st.boot_until_us);
  consider(st.rescan_due_us);
  consider(st.screen_until_us);
  if (st.setup_pages_up) consider(st.setup_page_at_us);
  if (!st.current && !st.paused && !st.status_reason.empty()) consider(st.retry_at_us);
  return wait;
}

cJSON *item_json(const content::HistoryItem &item, size_t index, bool current) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "index", static_cast<double>(index));
  cJSON_AddStringToObject(o, "kind", item.kind == content::ItemKind::Interlude ? "interlude" : "artwork");
  if (item.kind == content::ItemKind::Interlude) cJSON_AddStringToObject(o, "widget", g_env->widget_name(static_cast<system::WidgetKind>(item.widget)));
  const char *source = "channel";
  switch (item.source) {
    case content::Source::Channel: source = "channel"; break;
    case content::Source::PlayThisFile: source = "file"; break;
    case content::Source::PlayThisUrl: source = "url"; break;
    case content::Source::PlayThisMakapix: source = "makapix"; break;
  }
  cJSON_AddStringToObject(o, "source", source);
  cJSON_AddStringToObject(o, "name", item.name.c_str());
  cJSON_AddStringToObject(o, "path", relative_path(item.path).c_str());
  cJSON_AddStringToObject(o, "channel", item.channel.c_str());
  cJSON_AddNumberToObject(o, "channel_index", item.channel_index);
  cJSON_AddStringToObject(o, "playset", item.playset.c_str());
  if (item.post_id >= 0) cJSON_AddNumberToObject(o, "post_id", item.post_id);
  if (!item.sqid.empty()) cJSON_AddStringToObject(o, "sqid", item.sqid.c_str());
  cJSON_AddNumberToObject(o, "shown_s_ago", static_cast<double>((now_us() - item.shown_at_us) / kSecond));
  cJSON_AddBoolToObject(o, "current", current);
  return o;
}

void settings_changed_impl() {
      const system::Settings s = settings();
      apply_scheduler_modes(s);
      if (st.current) st.current->set_background(s.background);
      if (st.prepared) st.prepared->set_background(s.background);
      if (s.main_state != g_main_state) {
        g_main_state = s.main_state;
        LOGI("main state: %s", s.main_state == system::MainState::Widget ? "widget" : s.main_state == system::MainState::Stream ? "stream" : "animation show");
        if (s.main_state == system::MainState::Widget) {
          st.want_prepared_now = false;  // a pick in flight stays prepared, not played
          if (can_swap_now()) {
            play_widget(s.widget, false);
          } else {
            st.want_widget = true;
          }
        } else if (s.main_state == system::MainState::Stream) {
          st.want_prepared_now = false;
          st.want_widget = false;
          show_stream_waiting();
        } else {
          st.want_widget = false;
          st.widget.reset();
          st.widget_up = false;
          end_screen();
        }
      } else if (g_main_state == system::MainState::Widget && st.widget_up) {
        // A changed widget, or changed widget settings (face, seconds, font, units): the
        // source restarts so the new look shows at once instead of at its next frame.
        play_widget(s.widget, false);
      }
      if (g_stream_up && !stream_allowed()) {
        release_stream();
      } else if (!g_stream_up) {
        take_stream();
      }
}


}  // namespace

void init(ShowEnv &env, gfx::Frame &scratch) {
  g_env = &env;
  g_scratch = &scratch;
  st = State{};
  g_main_state = system::MainState::AnimationShow;
  g_artwork_up = false;
  g_stream_up = false;
}

const State &state() { return st; }

void boot(std::shared_ptr<playback::FrameSource> source, uint32_t boot_ms) {
  if (boot_ms > 0) st.boot_until_us = now_us() + static_cast<int64_t>(boot_ms) * 1000;
  present(std::move(source));
}

void restore(const std::string &saved_name) {
  std::string name = saved_name;
  content::Playset p;
  std::string error;
  content::Builtin b;
  const bool followed = content::builtin_from_name(name, b) && b == content::Builtin::Followed;
  if (name.empty() || (!followed && !resolve_playset(name, p, error)) || (followed && !g_env->makapix_paired())) {
    if (!name.empty()) LOGW("saved playset %s cannot be restored (%s)", name.c_str(), error.c_str());
    // Spec 5.2: Promoted, or Local when there is no network. Promoted plays anonymously
    // from its cache once the first refresh ran; Local is the safe start with a card.
    name = g_env->card_mounted() ? content::builtin_name(content::Builtin::Local)
                              : content::builtin_name(content::Builtin::Promoted);
  }
  activate_playset(name, false);
  const system::Settings s = settings();
  g_main_state = s.main_state;
  if (s.main_state == system::MainState::Widget) st.want_widget = true;
  if (s.main_state == system::MainState::Stream) show_stream_waiting();
}

void next() { do_next(); }
void previous() { do_previous(); }
void go_to(size_t history_index) { do_go_to(history_index); }
void pause() { do_pause(); }
void resume() { do_resume(); }
void reset_timer() { st.swap_at_us = now_us(); }
void refresh() { st.rescan_due_us = now_us(); }
void play_file(const std::string &path, int32_t post_id, const std::string &name) { do_play_file(path, post_id, name); }

void activate(const std::string &name) {
  enter_animation_show("playset");
  activate_playset(name, true);
}

void activate_transient(const content::Playset &playset) {
  enter_animation_show("playset from the site");
  do_activate_transient(playset);
}

void on_loaded(std::unique_ptr<loader::LoadResult> result) { on_loaded_impl(std::move(result)); }
void on_scanned(std::unique_ptr<loader::ScanResult> result) { on_scanned_impl(std::move(result)); }
void card_changed() { st.rescan_due_us = now_us() + kSecond / 5; }
void files_changed() { st.rescan_due_us = now_us() + kRescanDebounceUs; }
void settings_changed() { settings_changed_impl(); }
void makapix_changed() { on_makapix_changed(); }
void makapix_state(makapix::State state) { on_makapix_state(static_cast<uint32_t>(state)); }

void wifi_connected() {
  // The IP goes on the panel only when nothing is playing yet (spec 15.1).
  if (!st.current && !st.paused && st.screen == Screen::None) show_screen(Screen::Connected, 15 * kSecond);
  update_counts();
}

void stream_started() {
  st.stream_active = true;
  take_stream();
}

void stream_ended() {
  st.stream_active = false;
  release_stream();
}

void tick() { tick_impl(); }
int64_t wait_us() { return wait_us_impl(); }

bool overlay_allowed() { return g_artwork_up.load() && !g_stream_up.load() && show_active(); }
bool is_paused() { return st.paused; }

int32_t current_post_id() {
  const content::HistoryItem *cur = st.history.current();
  return (st.current && cur) ? cur->post_id : -1;
}

const std::string &active_playset_name() { return st.playset.name; }

cJSON *status_json() {
  const system::Settings s = settings();
  const int64_t now = now_us();
  cJSON *p = cJSON_CreateObject();
  cJSON_AddStringToObject(p, "state", g_main_state == system::MainState::Widget ? "widget" : g_main_state == system::MainState::Stream ? "stream" : "animation_show");
  cJSON_AddBoolToObject(p, "paused", st.paused);
  cJSON_AddBoolToObject(p, "stream_up", g_stream_up.load());
  if (st.widget_up) cJSON_AddStringToObject(p, "widget", g_env->widget_name(st.widget_kind));
  cJSON *ps = cJSON_AddObjectToObject(p, "playset");
  cJSON_AddStringToObject(ps, "name", st.playset.name.c_str());
  cJSON_AddBoolToObject(ps, "builtin", st.playset.builtin);
  cJSON_AddNumberToObject(ps, "channels", static_cast<double>(st.channels.size()));
  cJSON_AddBoolToObject(ps, "scanning", st.scan_running);
  cJSON_AddNumberToObject(ps, "version", st.channels_version);
  if (st.current) {
    cJSON *a = cJSON_AddObjectToObject(p, "artwork");
    cJSON_AddStringToObject(a, "name", st.current->name().c_str());
    cJSON_AddStringToObject(a, "format", decode::format_name(st.current->format()));
    cJSON_AddNumberToObject(a, "width", st.current->info().width);
    cJSON_AddNumberToObject(a, "height", st.current->info().height);
    cJSON_AddNumberToObject(a, "bytes", static_cast<double>(st.current->file_bytes()));
    cJSON_AddBoolToObject(a, "animated", st.current->info().animated);
    cJSON_AddNumberToObject(a, "frames_decoded", st.current->frames_decoded());
    cJSON_AddNumberToObject(a, "since_s", static_cast<double>((now - st.swap_at_us) / kSecond));
    if (const content::HistoryItem *cur = st.history.current()) {
      cJSON_AddStringToObject(a, "path", relative_path(cur->path).c_str());
      cJSON_AddStringToObject(a, "channel", cur->channel.c_str());
      cJSON_AddNumberToObject(a, "channel_index", cur->channel_index);
      const char *source = cur->source == content::Source::Channel ? "channel" : "play_this";
      cJSON_AddStringToObject(a, "source", source);
      if (cur->post_id >= 0) cJSON_AddNumberToObject(a, "post_id", cur->post_id);
      if (!cur->sqid.empty()) cJSON_AddStringToObject(a, "sqid", cur->sqid.c_str());
    }
  }
  const bool setup_up = st.screen == Screen::Setup || (st.setup_pages_up && !st.current && !st.widget_up);
  const char *screen = st.screen == Screen::Pairing     ? "pairing"
                       : st.screen == Screen::Paired    ? "paired"
                       : st.screen == Screen::Connected ? "connected"
                       : st.screen == Screen::Update    ? "update"
                       : setup_up                       ? "setup"
                                                        : "";
  cJSON_AddStringToObject(p, "screen", screen);
  cJSON_AddStringToObject(p, "no_artwork", st.current || st.paused || st.widget_up || st.screen != Screen::None ? "" : st.status_reason.c_str());
  cJSON_AddStringToObject(p, "last_error", st.last_error.c_str());
  cJSON *h = cJSON_AddObjectToObject(p, "history");
  cJSON_AddNumberToObject(h, "count", static_cast<double>(st.history.size()));
  cJSON_AddNumberToObject(h, "position", st.history.size() ? static_cast<double>(st.history.position()) : -1);
  cJSON_AddBoolToObject(h, "can_back", st.history.can_back());
  cJSON_AddBoolToObject(h, "can_forward", st.history.can_forward());
  cJSON *as = cJSON_AddObjectToObject(p, "auto_swap");
  cJSON_AddNumberToObject(as, "interval_s", s.auto_swap_seconds);
  const int64_t remaining =
      (!st.paused && st.current && s.auto_swap_seconds) ? st.swap_at_us + s.auto_swap_seconds * kSecond - now : -kSecond;
  cJSON_AddNumberToObject(as, "remaining_s", remaining < 0 ? -1 : static_cast<double>(remaining / kSecond));
  cJSON_AddBoolToObject(p, "prepared", static_cast<bool>(st.prepared));
  cJSON_AddNumberToObject(p, "swaps", st.swaps);
  cJSON_AddNumberToObject(p, "load_failures", st.load_failures);
  const ShowEnv::RenderTotals r = g_env->render_totals();
  cJSON_AddNumberToObject(p, "frames", r.frames);
  cJSON_AddNumberToObject(p, "late", r.late);
  cJSON_AddNumberToObject(p, "skipped", r.skipped);
  return p;
}

cJSON *channels_json() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "playset", st.playset.name.c_str());
  cJSON_AddBoolToObject(root, "scanning", st.scan_running);
  cJSON_AddNumberToObject(root, "version", st.channels_version);
  cJSON_AddNumberToObject(root, "last_scan_ms", st.last_scan_ms);
  cJSON *arr = cJSON_AddArrayToObject(root, "channels");
  for (size_t i = 0; i < st.channels.size(); ++i) {
    const ChannelRuntime &ch = st.channels[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "index", static_cast<double>(i));
    cJSON_AddStringToObject(o, "kind", content::kind_name(ch.spec.kind));
    cJSON_AddStringToObject(o, "identifier", ch.spec.identifier.c_str());
    cJSON_AddStringToObject(o, "display_name", display_name(ch.spec).c_str());
    cJSON_AddNumberToObject(o, "weight", ch.spec.weight);
    cJSON_AddNumberToObject(o, "offset", ch.spec.offset);
    cJSON_AddNumberToObject(o, "entries", static_cast<double>(ch.makapix ? ch.mk_entries.size() : ch.entries.size()));
    cJSON_AddNumberToObject(o, "available", ch.available);
    cJSON_AddStringToObject(o, "status", ch.status.c_str());
    if (ch.makapix) {
      makapix::ChannelSnapshot snap;
      if (g_env->makapix_snapshot(ref_of(ch.spec), snap)) {
        cJSON_AddNumberToObject(o, "cached", snap.cached);
        cJSON_AddNumberToObject(o, "last_refresh", snap.last_refresh);
        cJSON_AddNumberToObject(o, "oversized", snap.oversized);
        cJSON_AddBoolToObject(o, "refreshing", snap.refreshing);
        cJSON_AddStringToObject(o, "error", snap.error.c_str());
      }
    }
    if (i < st.scheduler.size()) {
      const content::Scheduler::Channel &sc = st.scheduler.channel(i);
      cJSON_AddNumberToObject(o, "share", static_cast<double>(sc.weight) / content::Scheduler::kWeightSum);
      cJSON_AddNumberToObject(o, "credit", sc.credit);
      cJSON_AddNumberToObject(o, "cursor", sc.cursor);
    }
    cJSON_AddItemToArray(arr, o);
  }
  return root;
}

cJSON *history_json() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "count", static_cast<double>(st.history.size()));
  cJSON_AddNumberToObject(root, "position", st.history.size() ? static_cast<double>(st.history.position()) : -1);
  cJSON *arr = cJSON_AddArrayToObject(root, "items");
  for (size_t i = 0; i < st.history.size(); ++i) {
    cJSON_AddItemToArray(arr, item_json(st.history.at(i), i, i == st.history.position() && (st.current || st.paused)));
  }
  return root;
}

}  // namespace p64::show::core

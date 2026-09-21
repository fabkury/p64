#include "show.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "boot_animation.hpp"
#include "loader.hpp"
#include "p64/content/history.hpp"
#include "p64/content/local_index.hpp"
#include "p64/content/makapix_index.hpp"
#include "p64/content/playset.hpp"
#include "p64/content/playset_json.hpp"
#include "p64/content/playset_store.hpp"
#include "p64/content/psram.hpp"
#include "p64/content/scheduler.hpp"
#include "p64/makapix/makapix.hpp"
#include "p64/net/wifi.hpp"
#include "p64/playback/frame_source.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"
#include "p64/system/state_store.hpp"
#include "p64/stream/stream.hpp"
#include "p64/web/web.hpp"
#include "p64/widgets/widgets.hpp"
#include "status_screens.hpp"

namespace p64::show {
namespace {

constexpr const char *TAG = "show";
constexpr const char *kActiveKey = "playset";
constexpr int64_t kSecond = 1000000;
constexpr int64_t kRescanDebounceUs = 2 * kSecond;   // after file manager changes
constexpr int64_t kRetryUs = 5 * kSecond;            // while nothing can be shown
constexpr int64_t kIdleRescanUs = 30 * kSecond;      // rescan period while nothing can be shown
constexpr int64_t kPairedScreenUs = 10 * kSecond;    // spec 6.4
constexpr uint32_t kMaxPrepareFailures = 20;         // consecutive load failures before giving up on a pick

// --- commands -------------------------------------------------------------------------

enum class Cmd : uint8_t {
  Next,
  Previous,
  GoTo,
  Pause,
  Resume,
  ResetTimer,
  Refresh,
  PlayFile,
  PlayDownloaded,
  Activate,
  ActivateTransient,
  Loaded,
  Scanned,
  CardChanged,
  FilesChanged,
  Settings,
  MakapixChanged,
  MakapixState,
  WifiConnected,
  StreamStarted,
  StreamEnded,
};

struct Command {
  Cmd type;
  uint32_t number;
  std::string *text;
  std::string *text2;
  loader::LoadResult *load;
  loader::ScanResult *scan;
  content::Playset *playset;
};

// The boot animation as a source: runs its course, then holds its last frame until the
// first artwork replaces it.
class BootSource : public playback::FrameSource {
 public:
  explicit BootSource(uint32_t duration_ms) : duration_ms_(duration_ms), t0_(esp_timer_get_time()) {}
  const std::string &name() const override { return name_; }
  bool next_frame(gfx::Frame &out, uint32_t &delay_ms, int64_t due_us) override {
    const int64_t at = due_us ? due_us : esp_timer_get_time();
    uint32_t t_ms = at > t0_ ? static_cast<uint32_t>((at - t0_) / 1000) : 0;
    const bool over = t_ms + 1 >= duration_ms_;
    if (over) t_ms = duration_ms_ - 1;
    animation_.render(out, t_ms, duration_ms_);
    // 30 fps: the procedural render (about 7 ms) plus the 7.5 ms panel copy share
    // core 1, so 60 fps would run late on every frame and pollute the counters.
    delay_ms = over ? 100 : 33;
    return true;
  }
  bool is_static() const override { return false; }

 private:
  std::string name_ = "boot";
  BootAnimation animation_;
  uint32_t duration_ms_;
  int64_t t0_;
};

struct ChannelRuntime {
  content::ChannelSpec spec;
  bool makapix = false;
  content::LocalEntries entries;      // local channels
  content::MakapixEntries mk_entries; // Makapix channels: the index
  std::vector<uint16_t> mk_cached;    // indexes into mk_entries of the cached ones (pickable)
  uint32_t available = 0;             // entries neither missing nor rejected (local) or cached (Makapix)
  std::string status;                 // "" when the channel can supply artworks, else why not
};

struct Pick {
  uint32_t generation = 0;  // the playset generation it was made against
  int channel = -1;
  int entry = -1;  // index in the channel's pickable list
  uint32_t pool = 0;  // pickable entries in the channel at pick time
  bool makapix = false;
  content::MakapixEntry mk_entry = {};
  std::string path;
  std::string name;
  std::string channel_name;
};

struct Pending {
  enum class Purpose : uint8_t { None, Navigate, PlayThis, Resume } purpose = Purpose::None;
  uint32_t load_id = 0;
  int direction = 0;  // navigation: -1 previous, +1 next, 0 exact
  content::HistoryItem item;
};

enum class Screen : uint8_t { None, Pairing, Paired, Connected };

// The state. Owned by the main task; g_mutex makes it readable by the API's tasks.
std::mutex g_mutex;
QueueHandle_t g_commands = nullptr;
playback::Player *g_player = nullptr;
playback::Renderer *g_renderer = nullptr;
gfx::Frame *g_scratch = nullptr;  // PSRAM; status screens are drawn here

content::Playset g_playset;
std::vector<ChannelRuntime> g_channels;
// Bumped whenever the channel list or its counts change (a scan installed, a Makapix
// index or cache change applied): the web UI refetches /api/v1/channels when the number
// in the status document moves.
uint32_t g_channels_version = 0;
content::Scheduler g_scheduler;
content::History g_history;
uint32_t g_generation = 0;  // bumps per scan request; results carry it back

bool g_paused = false;
int64_t g_swap_at_us = 0;                    // when the current item went up
int64_t g_boot_until_us = 0;                 // the boot animation holds the panel until then
std::shared_ptr<playback::Artwork> g_current;  // the artwork on the panel (null: status screen or pause)
std::string g_status_reason;                 // the "no artwork" reason on the panel ("" when none)
std::string g_last_error;                    // the last load or activation failure, for the UI
Screen g_screen = Screen::None;              // a status screen that holds the panel
int64_t g_screen_until_us = 0;               // when a timed screen ends (0 = until its cause ends)
std::atomic<system::MainState> g_main_state{system::MainState::AnimationShow};  // read by the overlay hook
std::shared_ptr<playback::FrameSource> g_widget;  // the widget on the panel (Widget state or an interlude)
bool g_widget_up = false;
system::WidgetKind g_widget_kind = system::WidgetKind::Clock;
bool g_want_widget = false;                  // the Widget state waits for the boot animation
std::atomic<bool> g_artwork_up{false};       // read by the overlay hook on the player task
std::shared_ptr<playback::FrameSource> g_on_panel;  // the last source the show handed to the player
std::shared_ptr<playback::FrameSource> g_behind;    // what the state put up while a stream holds the panel
std::atomic<bool> g_stream_up{false};        // a stream holds the panel (read by the overlay hook)
bool g_stream_active = false;                // stream frames are arriving (StreamStarted .. StreamEnded)
bool g_want_stream = false;                  // a takeover waits for the boot animation or a screen

Pick g_prepared_pick;
std::shared_ptr<playback::Artwork> g_prepared;
uint32_t g_prepared_load_id = 0;
bool g_want_prepared_now = false;
uint32_t g_prepare_failures = 0;
Pending g_pending;

bool g_scan_running = false;
bool g_scan_again = false;
bool g_activate_after_scan = false;
int64_t g_rescan_due_us = 0;
int64_t g_retry_at_us = 0;
uint32_t g_swaps = 0;
uint32_t g_load_failures = 0;
uint32_t g_last_scan_ms = 0;

int64_t now_us() { return esp_timer_get_time(); }

template <typename T, typename... Args>
std::shared_ptr<T> psram_shared(Args &&...args) {
  return std::allocate_shared<T>(content::PsramAllocator<T>(), std::forward<Args>(args)...);
}

// Every source the show puts up goes through here. While a stream holds the panel the
// source is kept aside instead (the state keeps running invisibly, spec 8.3) and goes up
// when the stream ends.
void present(std::shared_ptr<playback::FrameSource> src) {
  if (g_stream_up) {
    g_behind = std::move(src);
    return;
  }
  g_on_panel = src;
  g_player->play(std::move(src));
}

void send(Cmd type, uint32_t number = 0, std::string *text = nullptr, loader::LoadResult *load = nullptr,
          loader::ScanResult *scan = nullptr, std::string *text2 = nullptr, content::Playset *playset = nullptr) {
  Command c{type, number, text, text2, load, scan, playset};
  if (!g_commands || xQueueSend(g_commands, &c, pdMS_TO_TICKS(500)) != pdTRUE) {
    ESP_LOGW(TAG, "command queue full; dropped command %d", static_cast<int>(type));
    delete text;
    delete text2;
    delete load;
    delete scan;
    delete playset;
  }
}

std::string relative_path(const std::string &absolute) {
  const std::string prefix = storage::root() + "/";
  return absolute.rfind(prefix, 0) == 0 ? absolute.substr(prefix.size()) : absolute;
}

std::string basename_of(const std::string &path) { return path.substr(path.rfind('/') + 1); }

std::string channel_dir(const content::ChannelSpec &spec) {
  return spec.identifier.empty() ? storage::animations_dir() : storage::animations_dir() + "/" + spec.identifier;
}

makapix::ChannelRef ref_of(const content::ChannelSpec &spec) { return makapix::ChannelRef{spec.kind, spec.identifier}; }

std::string display_name(const content::ChannelSpec &spec) {
  return spec.display_name.empty() ? spec.default_display_name() : spec.display_name;
}

// Why a channel cannot supply artworks right now ("" when it can).
std::string channel_status(const ChannelRuntime &ch) {
  const content::ChannelSpec &spec = ch.spec;
  if (!spec.supported()) return "not supported yet";
  if (spec.kind == content::ChannelKind::Local) return storage::mounted() ? "" : "no card";
  const makapix::Status ms = makapix::status();
  if (spec.needs_pairing() && ms.state != makapix::State::Paired) return "needs pairing";
  if (ch.mk_cached.empty()) {
    if (!ch.mk_entries.empty()) return "downloading";
    if (!ms.online) return "offline";
    return "no listing yet";
  }
  return "";
}

// Why nothing can be shown: the first reason among the channels, "empty" when a usable
// channel simply has no files.
std::string no_artwork_reason() {
  std::string reason;
  for (const ChannelRuntime &ch : g_channels) {
    if (ch.status.empty()) {
      if (ch.available > 0) return "";
      if (reason.empty()) reason = "empty";
    } else if (reason.empty()) {
      reason = ch.status;
    }
  }
  return reason.empty() ? "empty" : reason;
}

bool can_swap_now() { return now_us() >= g_boot_until_us && g_screen == Screen::None; }

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
  ESP_LOGI(TAG, "main state: animation show (%s)", why);
  g_main_state = system::MainState::AnimationShow;
  g_want_widget = false;
  g_swap_at_us = now_us();  // a widget still up while the artwork loads gets a full interval
  system::settings_update([](system::Settings &s) { s.main_state = system::MainState::AnimationShow; });
  if (g_stream_up && !stream_allowed()) release_stream();
}

bool has_local_channels() {
  for (const ChannelRuntime &ch : g_channels) {
    if (!ch.makapix) return true;
  }
  return false;
}

void apply_scheduler_modes(const system::Settings &s) {
  g_scheduler.set_pick_mode(s.pick_mode == system::PickMode::Recency ? content::PickMode::Recency
                                                                      : content::PickMode::Random);
  g_scheduler.set_channel_select(s.channel_select == system::ChannelSelect::Swrr ? content::ChannelSelect::Swrr
                                                                                  : content::ChannelSelect::Stochastic);
}

void update_counts() {
  for (size_t i = 0; i < g_channels.size(); ++i) {
    ChannelRuntime &ch = g_channels[i];
    ch.status = channel_status(ch);
    const uint32_t count = ch.makapix ? static_cast<uint32_t>(ch.mk_cached.size())
                                      : (ch.available ? static_cast<uint32_t>(ch.entries.size()) : 0);
    g_scheduler.set_count(i, ch.status.empty() ? count : 0);
  }
}

// Pulls the Makapix channels' indexes from the Makapix component.
void snapshot_makapix(ChannelRuntime &ch) {
  makapix::ChannelSnapshot snap;
  ch.mk_entries.clear();
  ch.mk_cached.clear();
  if (makapix::snapshot(ref_of(ch.spec), snap)) {
    ch.mk_entries = std::move(snap.entries);
    // Pickable: cached and within the size limit (an index walked before the limit was
    // lowered still lists bigger artworks until its refresh lands).
    const uint16_t max_side = system::settings().makapix_max_side;
    for (size_t i = 0; i < ch.mk_entries.size() && i < 65535; ++i) {
      const content::MakapixEntry &e = ch.mk_entries[i];
      if ((e.flags & content::kMakapixCached) && content::fits_side(e, max_side)) ch.mk_cached.push_back(static_cast<uint16_t>(i));
    }
  }
  ch.available = static_cast<uint32_t>(ch.mk_cached.size());
}

// --- picking and playing --------------------------------------------------------------

bool pick_fresh(Pick &out) {
  uint32_t attempts = g_scheduler.available_channels();
  const content::HistoryItem *cur = g_history.current();
  while (attempts-- > 0) {
    const int c = g_scheduler.select_channel();
    if (c < 0 || static_cast<size_t>(c) >= g_channels.size()) return false;
    ChannelRuntime &ch = g_channels[c];
    const int avoid = (cur && cur->channel_index == c && cur->playset == g_playset.name) ? cur->entry_index : -1;
    const size_t pool = ch.makapix ? ch.mk_cached.size() : ch.entries.size();
    const size_t tries = std::min<size_t>(pool, 16);
    for (size_t t = 0; t < tries; ++t) {
      const int e = g_scheduler.pick_entry(c, avoid);
      if (e < 0 || static_cast<size_t>(e) >= pool) break;
      out.generation = g_generation;
      out.channel = c;
      out.entry = e;
      out.pool = static_cast<uint32_t>(pool);
      out.channel_name = display_name(ch.spec);
      if (ch.makapix) {
        const content::MakapixEntry &entry = ch.mk_entries[ch.mk_cached[e]];
        out.makapix = true;
        out.mk_entry = entry;
        out.path = makapix::artwork_path(entry);
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
  if (pick.generation != g_generation || pick.channel < 0 || static_cast<size_t>(pick.channel) >= g_channels.size()) return;
  ChannelRuntime &ch = g_channels[pick.channel];
  if (pick.makapix) {
    makapix::note_load_failed(pick.mk_entry, missing);
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
  if (ch.available == 0) g_scheduler.set_count(pick.channel, 0);
}

void request_prepare() {
  if (g_prepared || g_prepared_load_id) return;
  Pick p;
  if (!pick_fresh(p)) return;
  g_prepared_pick = p;
  g_prepared_load_id = loader::load(p.path, system::settings().background);
}

void report_shown(const content::HistoryItem &item) {
  if (item.post_id < 0) {
    makapix::note_hidden();
    return;
  }
  makapix::ChannelRef ref{static_cast<content::ChannelKind>(item.channel_kind), item.channel_identifier};
  const bool from_channel = item.source == content::Source::Channel;
  makapix::note_shown(item.post_id, from_channel ? &ref : nullptr, item.source == content::Source::PlayThisMakapix);
}

void play_artwork(std::shared_ptr<playback::Artwork> art, content::HistoryItem item, bool push) {
  const int64_t now = now_us();
  present(art);
  g_current = std::move(art);
  g_widget.reset();
  g_widget_up = false;
  g_artwork_up = true;
  g_status_reason.clear();
  g_screen = Screen::None;
  g_paused = false;
  g_swap_at_us = now;
  item.shown_at_us = now;
  if (push) {
    g_history.push(std::move(item));
  } else if (content::HistoryItem *cur = g_history.current()) {
    cur->shown_at_us = now;
  }
  ++g_swaps;
  const decode::Info &info = g_current->info();
  const content::HistoryItem *cur = g_history.current();
  ESP_LOGI(TAG, "playing %s (%s %dx%d, %u bytes%s) from %s, history %u/%u", g_current->name().c_str(),
           decode::format_name(g_current->format()), info.width, info.height,
           static_cast<unsigned>(g_current->file_bytes()), info.animated ? "" : ", static",
           cur ? (cur->channel.empty() ? "play-this" : cur->channel.c_str()) : "?",
           static_cast<unsigned>(g_history.position() + 1), static_cast<unsigned>(g_history.size()));
  if (cur) report_shown(*cur);
  system::publish(system::Event::PlaybackSwapped, static_cast<int32_t>(g_history.position()));
}

void play_prepared() {
  content::HistoryItem item;
  item.kind = content::ItemKind::Artwork;
  item.source = content::Source::Channel;
  item.path = g_prepared_pick.path;
  item.name = g_prepared_pick.name;
  item.channel = g_prepared_pick.channel_name;
  item.channel_index = g_prepared_pick.channel;
  item.entry_index = g_prepared_pick.entry;
  item.playset = g_playset.name;
  if (g_prepared_pick.channel >= 0 && static_cast<size_t>(g_prepared_pick.channel) < g_channels.size()) {
    const content::ChannelSpec &spec = g_channels[g_prepared_pick.channel].spec;
    item.channel_kind = static_cast<uint8_t>(spec.kind);
    item.channel_identifier = spec.identifier;
  }
  if (g_prepared_pick.makapix) {
    item.post_id = g_prepared_pick.mk_entry.post_id;
    item.sqid = g_prepared_pick.mk_entry.sqid;
  }
  play_artwork(std::move(g_prepared), std::move(item), true);
  g_prepared.reset();
  g_want_prepared_now = false;
  request_prepare();
}

void show_frame(const char *name) {
  present(psram_shared<playback::StaticSource>(name, *g_scratch));
  g_current.reset();
  g_widget.reset();
  g_widget_up = false;
  g_artwork_up = false;
  g_paused = false;
  makapix::note_hidden();
  system::publish(system::Event::PlaybackSwapped, -1);
}

void show_status(const std::string &reason) {
  if (g_screen != Screen::None) return;  // a screen with a purpose holds the panel
  if (!g_current && !g_paused && g_status_reason == reason) return;
  status_screens::no_artwork(*g_scratch, reason);
  g_status_reason = reason;
  g_retry_at_us = now_us() + kRetryUs;
  if (!g_rescan_due_us && has_local_channels()) g_rescan_due_us = now_us() + kIdleRescanUs;
  ESP_LOGW(TAG, "no artwork: %s", reason.c_str());
  show_frame("no artwork");
}

void show_screen(Screen screen, int64_t for_us) {
  switch (screen) {
    case Screen::Pairing: status_screens::pairing_code(*g_scratch, makapix::status().code); break;
    case Screen::Paired: status_screens::paired(*g_scratch); break;
    case Screen::Connected: {
      const net::wifi::Status w = net::wifi::status();
      status_screens::connected(*g_scratch, w.hostname, w.ip);
      break;
    }
    case Screen::None: return;
  }
  g_screen = screen;
  g_screen_until_us = for_us ? now_us() + for_us : 0;
  g_status_reason.clear();
  show_frame("status");
}

void load_current_history_item(Pending::Purpose purpose, int direction);

// A widget takes the panel: the Widget state, an interlude, or an interlude revisited
// through history (spec 6.1, 6.2).
void play_widget(system::WidgetKind kind, bool interlude) {
  g_widget = widgets::make(kind);
  g_widget_kind = kind;
  present(g_widget);
  g_current.reset();
  g_artwork_up = false;
  g_widget_up = true;
  g_status_reason.clear();
  g_screen = Screen::None;
  g_paused = false;
  g_swap_at_us = now_us();
  g_want_widget = false;
  if (interlude) {
    content::HistoryItem item;
    item.kind = content::ItemKind::Interlude;
    item.source = content::Source::Channel;
    item.name = widgets::widget_name(kind);
    item.playset = g_playset.name;
    item.widget = static_cast<uint8_t>(kind);
    item.shown_at_us = g_swap_at_us;
    g_history.push(std::move(item));
  }
  makapix::note_hidden();
  ESP_LOGI(TAG, "widget %s%s", widgets::widget_name(kind), interlude ? " (interlude)" : "");
  system::publish(system::Event::PlaybackSwapped, static_cast<int32_t>(g_history.position()));
}

// At an auto-swap, each widget with an interlude probability is rolled in a fixed order;
// the first that wins takes the slot (spec 6.1).
bool roll_interlude() {
  const system::Settings s = system::settings();
  const struct {
    system::WidgetKind kind;
    uint8_t percent;
  } rolls[] = {{system::WidgetKind::Clock, s.interlude_clock},
               {system::WidgetKind::Weather, s.interlude_weather},
               {system::WidgetKind::Temperature, s.interlude_temperature}};
  for (const auto &r : rolls) {
    if (r.percent == 0) continue;
    if (esp_random() % 100 < r.percent) {
      play_widget(r.kind, true);
      return true;
    }
  }
  return false;
}

void swap_fresh();

void show_stream_waiting() {
  const net::wifi::Status w = net::wifi::status();
  const system::Settings s = system::settings();
  status_screens::stream_waiting(*g_scratch, w.hostname, w.connected ? w.ip : "no network", s.ddp_port, s.raw_udp_port);
  g_status_reason.clear();
  show_frame("stream waiting");
}

bool stream_allowed() {
  return g_main_state == system::MainState::Stream || system::settings().stream_takeover;
}

// The first complete frame of a stream takes the panel (spec 8.3). The boot animation
// and a screen that needs the user (pairing) finish first; informational screens are
// simply covered and their timers run on.
void take_stream() {
  if (!g_stream_active || g_stream_up || !stream_allowed()) return;
  if (now_us() < g_boot_until_us || g_screen == Screen::Pairing) {
    g_want_stream = true;
    return;
  }
  g_want_stream = false;
  g_behind = g_on_panel;
  g_stream_up = true;
  g_on_panel = stream::source();
  g_player->play(g_on_panel);
  makapix::note_hidden();
  const stream::Status st = stream::status();
  ESP_LOGI(TAG, "stream takes the panel: %s %dx%d from %s (behind: %s)", st.protocol.c_str(), st.width, st.height,
           st.sender.c_str(), g_behind ? g_behind->name().c_str() : "nothing");
  system::publish(system::Event::PlaybackSwapped, -1);
}

// The stream ended (silence) or may no longer hold the panel: back to what the state
// has up, which kept running meanwhile.
void release_stream() {
  g_want_stream = false;
  if (!g_stream_up) return;
  g_stream_up = false;
  std::shared_ptr<playback::FrameSource> back = std::move(g_behind);
  g_behind.reset();
  stream::wake();
  if (back) {
    present(back);
  } else if (g_main_state == system::MainState::Stream) {
    show_stream_waiting();
  } else {
    swap_fresh();
  }
  if (g_current) {
    if (const content::HistoryItem *cur = g_history.current()) report_shown(*cur);
  }
  ESP_LOGI(TAG, "stream released the panel: back to %s", g_on_panel ? g_on_panel->name().c_str() : "?");
  system::publish(system::Event::PlaybackSwapped, g_current ? static_cast<int32_t>(g_history.position()) : -1);
}

// A fresh pick goes up now (auto-swap, next at the end of history, activation). When
// nothing is ready, whatever is on the panel stays until something is (spec 4.5).
void swap_fresh() {
  if (g_prepared) {
    if (can_swap_now()) {
      play_prepared();
    } else {
      g_want_prepared_now = true;
    }
    return;
  }
  if (g_prepared_load_id) {
    g_want_prepared_now = true;
    return;
  }
  Pick p;
  if (pick_fresh(p)) {
    g_prepared_pick = p;
    g_prepared_load_id = loader::load(p.path, system::settings().background);
    g_want_prepared_now = true;
    return;
  }
  if (!g_current) {
    show_status(no_artwork_reason());
  } else if (!g_rescan_due_us && has_local_channels()) {
    g_rescan_due_us = now_us() + kRetryUs;  // the index may be stale; look again soon
  }
}

// Ends a timed status screen: back to the artwork that was up, or a fresh one.
void end_screen() {
  g_screen = Screen::None;
  g_screen_until_us = 0;
  if (g_history.current() && !g_paused) {
    load_current_history_item(Pending::Purpose::Resume, 0);
  } else {
    swap_fresh();
  }
}

void load_current_history_item(Pending::Purpose purpose, int direction) {
  const content::HistoryItem *item = g_history.current();
  if (!item) return;
  if (item->kind == content::ItemKind::Interlude) {
    play_widget(static_cast<system::WidgetKind>(item->widget), false);
    if (content::HistoryItem *cur = g_history.current()) cur->shown_at_us = now_us();
    return;
  }
  g_pending.purpose = purpose;
  g_pending.direction = direction;
  g_pending.item = *item;
  g_pending.load_id = loader::load(item->path, system::settings().background);
}

// --- scans and playsets ---------------------------------------------------------------

void start_scan(const content::Playset &playset, bool activation) {
  ++g_generation;
  g_scan_running = true;
  if (activation) g_activate_after_scan = true;
  loader::scan(g_generation, playset);
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
  const bool fresh = g_activate_after_scan || !same_channels(g_playset, r.playset) ||
                     g_channels.size() != r.playset.channels.size();
  g_playset = r.playset;
  std::vector<ChannelRuntime> channels(g_playset.channels.size());
  std::vector<makapix::ChannelRef> refs;
  size_t entries = 0;
  for (size_t i = 0; i < channels.size(); ++i) {
    ChannelRuntime &ch = channels[i];
    ch.spec = g_playset.channels[i];
    ch.makapix = ch.spec.is_makapix();
    if (ch.makapix) {
      refs.push_back(ref_of(ch.spec));
    } else {
      if (i < r.entries.size()) ch.entries = std::move(r.entries[i]);
      ch.available = static_cast<uint32_t>(ch.entries.size());
      if (i < r.errors.size() && !r.errors[i].empty() && storage::mounted()) ch.status = r.errors[i];
    }
    entries += ch.entries.size();
  }
  g_channels = std::move(channels);
  makapix::set_active_channels(refs);
  for (ChannelRuntime &ch : g_channels) {
    if (ch.makapix) snapshot_makapix(ch);
  }
  if (fresh) {
    std::vector<uint32_t> weights, offsets;
    for (const content::ChannelSpec &c : g_playset.channels) {
      weights.push_back(c.weight);
      offsets.push_back(c.offset);
    }
    const uint64_t seed = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    g_scheduler.configure(weights, offsets, seed);
  }
  apply_scheduler_modes(system::settings());
  update_counts();
  ++g_channels_version;
  web::notify();
  g_last_scan_ms = r.took_ms;
  ESP_LOGI(TAG, "playset %s: %u channels, %u local entries%s, scan %u ms%s", g_playset.name.c_str(),
           static_cast<unsigned>(g_channels.size()), static_cast<unsigned>(entries),
           r.skipped ? (", " + std::to_string(r.skipped) + " skipped").c_str() : "", r.took_ms, fresh ? " (fresh)" : "");
  if (fresh) {
    // The prepared artwork was picked against the old playset or index: drop it.
    g_prepared.reset();
    g_prepared_load_id = 0;
    g_prepared_pick = Pick{};
    g_want_prepared_now = false;
  }
  if (g_activate_after_scan) {
    g_activate_after_scan = false;
    if (show_active()) {
      swap_fresh();
    } else {
      request_prepare();  // the boot restore in Widget or Stream state: ready for later
    }
  } else if (!g_current && !g_paused && !g_widget_up && show_active()) {
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
  return content::store::load(name, out, error);
}

void activate(const std::string &name, bool persist) {
  content::Builtin b;
  if (content::builtin_from_name(name, b) && b == content::Builtin::Followed) {
    // The server generates this one; the Makapix component hands it back through
    // activate_transient() when it lands.
    std::string error;
    if (!makapix::play_followed(error)) {
      ESP_LOGW(TAG, "cannot activate Followed: %s", error.c_str());
      g_last_error = "Followed: " + error;
      return;
    }
    if (persist) system::state::set(kActiveKey, name);
    return;
  }
  content::Playset p;
  std::string error;
  if (!resolve_playset(name, p, error)) {
    ESP_LOGW(TAG, "cannot activate %s: %s", name.c_str(), error.c_str());
    g_last_error = "playset " + name + ": " + error;
    return;
  }
  ESP_LOGI(TAG, "activating playset %s", name.c_str());
  if (persist) system::state::set(kActiveKey, p.name);
  g_playset.name = p.name;  // status shows the new name while the scan runs
  g_playset.builtin = p.builtin;
  start_scan(p, true);
}

void do_activate_transient(const content::Playset &p) {
  ESP_LOGI(TAG, "activating playset %s (%u channels, from the site)", p.name.c_str(),
           static_cast<unsigned>(p.channels.size()));
  g_playset.name = p.name;
  g_playset.builtin = p.builtin;
  start_scan(p, true);
}

// --- command handling -----------------------------------------------------------------

void on_loaded(loader::LoadResult *raw) {
  std::unique_ptr<loader::LoadResult> res(raw);
  if (res->id == g_prepared_load_id) {
    g_prepared_load_id = 0;
    if (!res->artwork) {
      ESP_LOGW(TAG, "%s: %s", res->path.c_str(), res->error.c_str());
      ++g_load_failures;
      g_last_error = basename_of(res->path) + ": " + res->error;
      mark_entry(g_prepared_pick, res->missing);
      if (++g_prepare_failures < kMaxPrepareFailures) {
        request_prepare();
        if (g_want_prepared_now && g_prepared_load_id) return;
      }
      if (!g_current && !g_paused) show_status(no_artwork_reason());
      return;
    }
    g_prepare_failures = 0;
    g_prepared = res->artwork;
    ESP_LOGD(TAG, "prepared %s (read %u ms, open %u ms)", res->artwork->name().c_str(), res->read_ms, res->open_ms);
    if (g_want_prepared_now && can_swap_now() && show_active()) play_prepared();
    return;
  }
  if (res->id == g_pending.load_id) {
    g_pending.load_id = 0;
    const Pending::Purpose purpose = g_pending.purpose;
    g_pending.purpose = Pending::Purpose::None;
    if (!res->artwork) {
      ESP_LOGW(TAG, "%s: %s", res->path.c_str(), res->error.c_str());
      ++g_load_failures;
      g_last_error = basename_of(res->path) + ": " + res->error;
      if (purpose == Pending::Purpose::PlayThis) return;  // the current picture stays (spec 4.5)
      // A history item whose file is gone is dropped and the walk continues (spec 4.6).
      const size_t pos = g_history.position();
      if (g_history.size() && g_history.at(pos).path == res->path) g_history.remove(pos);
      if (purpose == Pending::Purpose::Navigate && g_pending.direction < 0 && g_history.can_back()) {
        g_history.back();
        load_current_history_item(purpose, -1);
      } else if (purpose == Pending::Purpose::Navigate && g_pending.direction > 0 && g_history.can_forward()) {
        g_history.forward();
        load_current_history_item(purpose, 1);
      } else if (!g_current || purpose == Pending::Purpose::Resume || g_pending.direction > 0) {
        swap_fresh();
      }
      return;
    }
    if (purpose == Pending::Purpose::Resume) {
      const content::HistoryItem *cur = g_history.current();
      if (g_current || !cur || cur->path != g_pending.item.path) {
        ESP_LOGD(TAG, "stale resume of %s ignored", res->path.c_str());
        return;  // something else went up meanwhile
      }
    }
    play_artwork(res->artwork, g_pending.item, purpose == Pending::Purpose::PlayThis);
    return;
  }
  // A result for a request that was superseded (playset changed, newer navigation).
  ESP_LOGD(TAG, "stale load result for %s ignored", res->path.c_str());
}

void on_scanned(loader::ScanResult *raw) {
  std::unique_ptr<loader::ScanResult> res(raw);
  g_scan_running = false;
  if (res->generation != g_generation) {
    ESP_LOGD(TAG, "stale scan result ignored");
  } else {
    install(*res);
  }
  if (g_scan_again) {
    g_scan_again = false;
    g_rescan_due_us = now_us();
  }
}

void on_makapix_changed() {
  bool any = false;
  for (ChannelRuntime &ch : g_channels) {
    if (!ch.makapix) continue;
    snapshot_makapix(ch);
    any = true;
  }
  if (!any) return;
  update_counts();
  ++g_channels_version;
  web::notify();
  // A pick prepared while the cache was still tiny (the same artwork again, or one of
  // a handful) is replaced once there is something to choose from.
  if (g_prepared && g_prepared_pick.makapix && g_prepared_pick.channel >= 0 &&
      static_cast<size_t>(g_prepared_pick.channel) < g_channels.size()) {
    const uint32_t pool = static_cast<uint32_t>(g_channels[g_prepared_pick.channel].mk_cached.size());
    const content::HistoryItem *cur = g_history.current();
    const bool repeat = cur && g_current && cur->post_id == g_prepared_pick.mk_entry.post_id;
    if (repeat || (g_prepared_pick.pool < 8 && pool > g_prepared_pick.pool)) {
      g_prepared.reset();
      g_prepared_pick = Pick{};
    }
  }
  if (!g_current && !g_widget_up && !g_paused && g_screen == Screen::None) {
    swap_fresh();
  } else if (!g_prepared && !g_prepared_load_id) {
    request_prepare();
  }
}

void on_makapix_state(uint32_t state) {
  const auto s = static_cast<makapix::State>(state);
  if (s == makapix::State::Pairing) {
    show_screen(Screen::Pairing, 0);
  } else if (g_screen == Screen::Pairing) {
    if (s == makapix::State::Paired) {
      show_screen(Screen::Paired, kPairedScreenUs);
    } else {
      end_screen();
    }
  }
  update_counts();
  if (s == makapix::State::Paired) g_rescan_due_us = now_us();  // channels that needed pairing can refresh
}

void do_next() {
  enter_animation_show("next");
  if (g_paused) g_paused = false;
  if (g_screen != Screen::None) end_screen();
  if (g_history.can_forward()) {
    g_history.forward();
    load_current_history_item(Pending::Purpose::Navigate, 1);
    return;
  }
  swap_fresh();
}

void do_previous() {
  if (g_paused) g_paused = false;
  if (!g_history.can_back()) {
    ESP_LOGI(TAG, "previous: at the start of history");
    return;
  }
  enter_animation_show("previous");
  g_history.back();
  load_current_history_item(Pending::Purpose::Navigate, -1);
}

void do_go_to(size_t index) {
  if (!g_history.go_to(index)) return;
  enter_animation_show("history");
  if (g_paused) g_paused = false;
  load_current_history_item(Pending::Purpose::Navigate, 0);
}

void do_pause() {
  if (g_paused) return;
  status_screens::black(*g_scratch);
  present(psram_shared<playback::StaticSource>("paused", *g_scratch));
  g_current.reset();
  g_widget.reset();
  g_widget_up = false;
  g_artwork_up = false;
  g_paused = true;
  makapix::note_hidden();
  ESP_LOGI(TAG, "paused");
  system::publish(system::Event::PlaybackSwapped, -1);
}

void do_resume() {
  if (!g_paused) return;
  enter_animation_show("resume");
  if (g_history.current()) {
    load_current_history_item(Pending::Purpose::Resume, 0);
  } else {
    g_paused = false;
    swap_fresh();
  }
}

void do_play_file(const std::string &path, int32_t post_id, const std::string &name) {
  enter_animation_show("play-this");
  content::HistoryItem item;
  item.kind = content::ItemKind::Artwork;
  item.source = post_id >= 0 ? content::Source::PlayThisMakapix
                             : (path.rfind(storage::downloads_dir(), 0) == 0 || path.rfind("mem:dl-", 0) == 0)
                                   ? content::Source::PlayThisUrl
                                   : content::Source::PlayThisFile;
  item.path = path;
  item.name = name.empty() ? basename_of(path) : name;
  item.playset = g_playset.name;
  item.post_id = post_id;
  g_pending.purpose = Pending::Purpose::PlayThis;
  g_pending.direction = 0;
  g_pending.item = std::move(item);
  g_pending.load_id = loader::load(path, system::settings().background);
}

void handle(Command &c) {
  switch (c.type) {
    case Cmd::Next: do_next(); break;
    case Cmd::Previous: do_previous(); break;
    case Cmd::GoTo: do_go_to(c.number); break;
    case Cmd::Pause: do_pause(); break;
    case Cmd::Resume: do_resume(); break;
    case Cmd::ResetTimer: g_swap_at_us = now_us(); break;
    case Cmd::Refresh: g_rescan_due_us = now_us(); break;
    case Cmd::PlayFile:
      if (c.text) do_play_file(*c.text, -1, "");
      break;
    case Cmd::PlayDownloaded:
      if (c.text) do_play_file(*c.text, static_cast<int32_t>(c.number), c.text2 ? *c.text2 : "");
      break;
    case Cmd::Activate:
      if (c.text) {
        enter_animation_show("playset");
        activate(*c.text, true);
      }
      break;
    case Cmd::ActivateTransient:
      if (c.playset) {
        enter_animation_show("playset from the site");
        do_activate_transient(*c.playset);
      }
      break;
    case Cmd::Loaded: on_loaded(c.load); break;
    case Cmd::Scanned: on_scanned(c.scan); break;
    case Cmd::CardChanged: g_rescan_due_us = now_us() + kSecond / 5; break;
    case Cmd::FilesChanged: g_rescan_due_us = now_us() + kRescanDebounceUs; break;
    case Cmd::Settings: {
      const system::Settings s = system::settings();
      apply_scheduler_modes(s);
      if (g_current) g_current->set_background(s.background);
      if (g_prepared) g_prepared->set_background(s.background);
      if (s.main_state != g_main_state) {
        g_main_state = s.main_state;
        ESP_LOGI(TAG, "main state: %s", s.main_state == system::MainState::Widget ? "widget" : s.main_state == system::MainState::Stream ? "stream" : "animation show");
        if (s.main_state == system::MainState::Widget) {
          g_want_prepared_now = false;  // a pick in flight stays prepared, not played
          if (can_swap_now()) {
            play_widget(s.widget, false);
          } else {
            g_want_widget = true;
          }
        } else if (s.main_state == system::MainState::Stream) {
          g_want_prepared_now = false;
          g_want_widget = false;
          show_stream_waiting();
        } else {
          g_want_widget = false;
          g_widget.reset();
          g_widget_up = false;
          end_screen();
        }
      } else if (g_main_state == system::MainState::Widget && g_widget_up) {
        // A changed widget, or changed widget settings (face, seconds, font, units): the
        // source restarts so the new look shows at once instead of at its next frame.
        play_widget(s.widget, false);
      }
      if (g_stream_up && !stream_allowed()) {
        release_stream();
      } else if (!g_stream_up) {
        take_stream();
      }
      break;
    }
    case Cmd::MakapixChanged: on_makapix_changed(); break;
    case Cmd::MakapixState: on_makapix_state(c.number); break;
    case Cmd::StreamStarted:
      g_stream_active = true;
      take_stream();
      break;
    case Cmd::StreamEnded:
      g_stream_active = false;
      release_stream();
      break;
    case Cmd::WifiConnected:
      // The IP goes on the panel only when nothing is playing yet (spec 15.1).
      if (!g_current && !g_paused && g_screen == Screen::None) show_screen(Screen::Connected, 15 * kSecond);
      update_counts();
      break;
  }
  delete c.text;
  delete c.text2;
  delete c.playset;
}

// The auto-swap timer runs while an artwork is up, whatever the state says (an artwork
// on the panel must never freeze), and while a widget is up inside the show (an
// interlude). The Widget state's own widget stays indefinitely (spec 6.2).
bool swap_timer_runs() {
  if (g_paused) return false;
  return static_cast<bool>(g_current) || (g_widget_up && show_active());
}

// Periodic work: the auto-swap timer, the boot hold, rescans, retries.
void tick() {
  const int64_t now = now_us();
  const system::Settings s = system::settings();
  if (g_screen != Screen::None && g_screen_until_us && now >= g_screen_until_us) end_screen();
  if (g_want_stream) take_stream();
  if (g_want_widget && now >= g_boot_until_us) play_widget(s.widget, false);
  if (g_want_prepared_now && g_prepared && can_swap_now() && show_active()) play_prepared();
  if (swap_timer_runs() && s.auto_swap_seconds > 0 &&
      now - g_swap_at_us >= static_cast<int64_t>(s.auto_swap_seconds) * kSecond) {
    if (!roll_interlude()) swap_fresh();
  }
  if (g_rescan_due_us && now >= g_rescan_due_us) {
    if (g_scan_running) {
      g_scan_again = true;
    } else {
      start_scan(g_playset, false);
    }
    g_rescan_due_us = 0;
  }
  if (show_active() && !g_current && !g_widget_up && !g_paused && g_screen == Screen::None && !g_status_reason.empty() &&
      now >= g_retry_at_us) {
    g_retry_at_us = now + kRetryUs;
    swap_fresh();
    if (!g_current && !g_rescan_due_us && !g_scan_running && has_local_channels()) g_rescan_due_us = now + kIdleRescanUs;
  }
  if (g_screen == Screen::Connected && g_screen_until_us == 0) end_screen();
}

// How long the loop may sleep before tick() has something to do.
TickType_t wait_ticks() {
  const int64_t now = now_us();
  int64_t wait = kSecond;  // settings changes and the like are noticed within a second
  auto consider = [&](int64_t at) {
    if (at > 0) wait = std::min(wait, std::max<int64_t>(at - now, 0));
  };
  const system::Settings s = system::settings();
  if (swap_timer_runs() && s.auto_swap_seconds > 0) {
    consider(g_swap_at_us + static_cast<int64_t>(s.auto_swap_seconds) * kSecond);
  }
  if (g_want_widget) consider(g_boot_until_us);
  if (g_want_prepared_now && g_prepared) consider(g_boot_until_us);
  consider(g_rescan_due_us);
  consider(g_screen_until_us);
  if (!g_current && !g_paused && !g_status_reason.empty()) consider(g_retry_at_us);
  const TickType_t ticks = pdMS_TO_TICKS(wait / 1000 + 1);
  return ticks ? ticks : 1;
}

cJSON *item_json(const content::HistoryItem &item, size_t index, bool current) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "index", static_cast<double>(index));
  cJSON_AddStringToObject(o, "kind", item.kind == content::ItemKind::Interlude ? "interlude" : "artwork");
  if (item.kind == content::ItemKind::Interlude) cJSON_AddStringToObject(o, "widget", widgets::widget_name(static_cast<system::WidgetKind>(item.widget)));
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

}  // namespace

// --- public API -----------------------------------------------------------------------

bool init(playback::Player &player, playback::Renderer &renderer, uint32_t boot_animation_ms) {
  g_player = &player;
  g_renderer = &renderer;
  void *mem = heap_caps_malloc(sizeof(gfx::Frame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  g_scratch = mem ? new (mem) gfx::Frame() : new gfx::Frame();
  g_commands = xQueueCreate(16, sizeof(Command));
  if (!g_commands) return false;
  if (!loader::start([](loader::LoadResult *r) { send(Cmd::Loaded, 0, nullptr, r, nullptr); },
                     [](loader::ScanResult *r) { send(Cmd::Scanned, 0, nullptr, nullptr, r); })) {
    ESP_LOGE(TAG, "loader task failed to start");
    return false;
  }
  // The boot animation holds the panel until it has run its course (spec 15.1).
  if (boot_animation_ms > 0) {
    g_boot_until_us = now_us() + static_cast<int64_t>(boot_animation_ms) * 1000;
    present(psram_shared<BootSource>(boot_animation_ms));
  } else {
    status_screens::black(*g_scratch);
    present(psram_shared<playback::StaticSource>("boot", *g_scratch));
  }
  g_player->set_overlay(playback::Player::Overlay{
      [] { return g_artwork_up.load() && !g_stream_up.load() && show_active() ? widgets::overlay_key() : 0u; },
      widgets::draw_overlay});
  system::subscribe(system::Event::CardMounted, [](const system::Message &) { send(Cmd::CardChanged); });
  system::subscribe(system::Event::CardFailed, [](const system::Message &) { send(Cmd::CardChanged); });
  system::subscribe(system::Event::LocalFilesChanged, [](const system::Message &) { send(Cmd::FilesChanged); });
  system::subscribe(system::Event::SettingsChanged, [](const system::Message &) { send(Cmd::Settings); });
  system::subscribe(system::Event::MakapixChannelChanged, [](const system::Message &) { send(Cmd::MakapixChanged); });
  system::subscribe(system::Event::MakapixStateChanged,
                    [](const system::Message &m) { send(Cmd::MakapixState, static_cast<uint32_t>(m.arg)); });
  system::subscribe(system::Event::WifiConnected, [](const system::Message &) { send(Cmd::WifiConnected); });
  system::subscribe(system::Event::WifiDisconnected, [](const system::Message &) { send(Cmd::MakapixChanged); });
  system::subscribe(system::Event::TimeSynced, [](const system::Message &) { send(Cmd::MakapixChanged); });
  system::subscribe(system::Event::StreamStarted, [](const system::Message &) { send(Cmd::StreamStarted); });
  system::subscribe(system::Event::StreamEnded, [](const system::Message &) { send(Cmd::StreamEnded); });
  return true;
}

void restore() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string name;
  system::state::get(kActiveKey, name);
  content::Playset p;
  std::string error;
  content::Builtin b;
  const bool followed = content::builtin_from_name(name, b) && b == content::Builtin::Followed;
  if (name.empty() || (!followed && !resolve_playset(name, p, error)) || (followed && !makapix::paired())) {
    if (!name.empty()) ESP_LOGW(TAG, "saved playset %s cannot be restored (%s)", name.c_str(), error.c_str());
    // Spec 5.2: Promoted, or Local when there is no network. Promoted plays anonymously
    // from its cache once the first refresh ran; Local is the safe start with a card.
    name = storage::mounted() ? content::builtin_name(content::Builtin::Local)
                              : content::builtin_name(content::Builtin::Promoted);
  }
  activate(name, false);
  const system::Settings s = system::settings();
  g_main_state = s.main_state;
  if (s.main_state == system::MainState::Widget) g_want_widget = true;
  if (s.main_state == system::MainState::Stream) show_stream_waiting();
}

[[noreturn]] void run() {
  ESP_LOGI(TAG, "show loop running");
  esp_task_wdt_add(nullptr);  // wait_ticks() is at most a second
  while (true) {
    esp_task_wdt_reset();
    Command c{};
    const bool got = xQueueReceive(g_commands, &c, wait_ticks()) == pdTRUE;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (got) handle(c);
    tick();
  }
}

void next() { send(Cmd::Next); }
void previous() { send(Cmd::Previous); }
void go_to(size_t history_index) { send(Cmd::GoTo, static_cast<uint32_t>(history_index)); }
void pause() { send(Cmd::Pause); }
void resume() { send(Cmd::Resume); }
void set_paused(bool paused) { send(paused ? Cmd::Pause : Cmd::Resume); }
void reset_timer() { send(Cmd::ResetTimer); }
void refresh() { send(Cmd::Refresh); }

bool play_file(const std::string &absolute_path, std::string &error) {
  if (!storage::exists(absolute_path) || storage::is_directory(absolute_path)) {
    error = "no such file";
    return false;
  }
  if (!content::artwork_extension(absolute_path.c_str())) {
    error = "not an artwork file (.gif, .png, .apng, .webp, .bmp)";
    return false;
  }
  send(Cmd::PlayFile, 0, new std::string(absolute_path));
  return true;
}

void play_downloaded(const std::string &path, int32_t post_id, const std::string &name) {
  send(Cmd::PlayDownloaded, static_cast<uint32_t>(post_id), new std::string(path), nullptr, nullptr,
       new std::string(name));
}

bool activate_playset(const std::string &name, std::string &error) {
  content::Builtin b;
  if (content::builtin_from_name(name, b)) {
    if (b == content::Builtin::Followed && !makapix::paired()) {
      error = "the Followed playset needs pairing";
      return false;
    }
  } else if (!content::store::exists(name)) {
    error = "no such playset";
    return false;
  }
  send(Cmd::Activate, 0, new std::string(name));
  return true;
}

void activate_transient(const content::Playset &playset) {
  send(Cmd::ActivateTransient, 0, nullptr, nullptr, nullptr, nullptr, new content::Playset(playset));
}

bool is_paused() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_paused;
}

int32_t current_post_id() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const content::HistoryItem *cur = g_history.current();
  return (g_current && cur) ? cur->post_id : -1;
}

cJSON *status_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const system::Settings s = system::settings();
  const int64_t now = now_us();
  cJSON *p = cJSON_CreateObject();
  cJSON_AddStringToObject(p, "state", g_main_state == system::MainState::Widget ? "widget" : g_main_state == system::MainState::Stream ? "stream" : "animation_show");
  cJSON_AddBoolToObject(p, "paused", g_paused);
  cJSON_AddBoolToObject(p, "stream_up", g_stream_up.load());
  if (g_widget_up) cJSON_AddStringToObject(p, "widget", widgets::widget_name(g_widget_kind));
  cJSON *ps = cJSON_AddObjectToObject(p, "playset");
  cJSON_AddStringToObject(ps, "name", g_playset.name.c_str());
  cJSON_AddBoolToObject(ps, "builtin", g_playset.builtin);
  cJSON_AddNumberToObject(ps, "channels", static_cast<double>(g_channels.size()));
  cJSON_AddBoolToObject(ps, "scanning", g_scan_running);
  cJSON_AddNumberToObject(ps, "version", g_channels_version);
  if (g_current) {
    cJSON *a = cJSON_AddObjectToObject(p, "artwork");
    cJSON_AddStringToObject(a, "name", g_current->name().c_str());
    cJSON_AddStringToObject(a, "format", decode::format_name(g_current->format()));
    cJSON_AddNumberToObject(a, "width", g_current->info().width);
    cJSON_AddNumberToObject(a, "height", g_current->info().height);
    cJSON_AddNumberToObject(a, "bytes", static_cast<double>(g_current->file_bytes()));
    cJSON_AddBoolToObject(a, "animated", g_current->info().animated);
    cJSON_AddNumberToObject(a, "frames_decoded", g_current->frames_decoded());
    cJSON_AddNumberToObject(a, "since_s", static_cast<double>((now - g_swap_at_us) / kSecond));
    if (const content::HistoryItem *cur = g_history.current()) {
      cJSON_AddStringToObject(a, "path", relative_path(cur->path).c_str());
      cJSON_AddStringToObject(a, "channel", cur->channel.c_str());
      cJSON_AddNumberToObject(a, "channel_index", cur->channel_index);
      const char *source = cur->source == content::Source::Channel ? "channel" : "play_this";
      cJSON_AddStringToObject(a, "source", source);
      if (cur->post_id >= 0) cJSON_AddNumberToObject(a, "post_id", cur->post_id);
      if (!cur->sqid.empty()) cJSON_AddStringToObject(a, "sqid", cur->sqid.c_str());
    }
  }
  const char *screen = g_screen == Screen::Pairing ? "pairing" : g_screen == Screen::Paired ? "paired"
                       : g_screen == Screen::Connected ? "connected" : "";
  cJSON_AddStringToObject(p, "screen", screen);
  cJSON_AddStringToObject(p, "no_artwork", g_current || g_paused || g_widget_up || g_screen != Screen::None ? "" : g_status_reason.c_str());
  cJSON_AddStringToObject(p, "last_error", g_last_error.c_str());
  cJSON *h = cJSON_AddObjectToObject(p, "history");
  cJSON_AddNumberToObject(h, "count", static_cast<double>(g_history.size()));
  cJSON_AddNumberToObject(h, "position", g_history.size() ? static_cast<double>(g_history.position()) : -1);
  cJSON_AddBoolToObject(h, "can_back", g_history.can_back());
  cJSON_AddBoolToObject(h, "can_forward", g_history.can_forward());
  cJSON *as = cJSON_AddObjectToObject(p, "auto_swap");
  cJSON_AddNumberToObject(as, "interval_s", s.auto_swap_seconds);
  const int64_t remaining =
      (!g_paused && g_current && s.auto_swap_seconds) ? g_swap_at_us + s.auto_swap_seconds * kSecond - now : -kSecond;
  cJSON_AddNumberToObject(as, "remaining_s", remaining < 0 ? -1 : static_cast<double>(remaining / kSecond));
  cJSON_AddBoolToObject(p, "prepared", static_cast<bool>(g_prepared));
  cJSON_AddNumberToObject(p, "swaps", g_swaps);
  cJSON_AddNumberToObject(p, "load_failures", g_load_failures);
  if (g_renderer) {
    const playback::Renderer::Stats r = g_renderer->totals();
    cJSON_AddNumberToObject(p, "frames", r.frames);
    cJSON_AddNumberToObject(p, "late", r.late);
    cJSON_AddNumberToObject(p, "skipped", r.skipped);
  }
  return p;
}

cJSON *channels_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "playset", g_playset.name.c_str());
  cJSON_AddBoolToObject(root, "scanning", g_scan_running);
  cJSON_AddNumberToObject(root, "version", g_channels_version);
  cJSON_AddNumberToObject(root, "last_scan_ms", g_last_scan_ms);
  cJSON *arr = cJSON_AddArrayToObject(root, "channels");
  for (size_t i = 0; i < g_channels.size(); ++i) {
    const ChannelRuntime &ch = g_channels[i];
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
      if (makapix::snapshot(ref_of(ch.spec), snap)) {
        cJSON_AddNumberToObject(o, "cached", snap.cached);
        cJSON_AddNumberToObject(o, "last_refresh", snap.last_refresh);
        cJSON_AddBoolToObject(o, "refreshing", snap.refreshing);
        cJSON_AddStringToObject(o, "error", snap.error.c_str());
      }
    }
    if (i < g_scheduler.size()) {
      const content::Scheduler::Channel &sc = g_scheduler.channel(i);
      cJSON_AddNumberToObject(o, "share", static_cast<double>(sc.weight) / content::Scheduler::kWeightSum);
      cJSON_AddNumberToObject(o, "credit", sc.credit);
      cJSON_AddNumberToObject(o, "cursor", sc.cursor);
    }
    cJSON_AddItemToArray(arr, o);
  }
  return root;
}

cJSON *history_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "count", static_cast<double>(g_history.size()));
  cJSON_AddNumberToObject(root, "position", g_history.size() ? static_cast<double>(g_history.position()) : -1);
  cJSON *arr = cJSON_AddArrayToObject(root, "items");
  for (size_t i = 0; i < g_history.size(); ++i) {
    cJSON_AddItemToArray(arr, item_json(g_history.at(i), i, i == g_history.position() && (g_current || g_paused)));
  }
  return root;
}

cJSON *playsets_json() {
  std::vector<content::store::Summary> stored;
  std::string error;
  const bool listed = content::store::list(stored, error);  // card I/O, outside the lock
  const bool paired = makapix::paired();
  const bool online = makapix::status().online;
  cJSON *root = cJSON_CreateObject();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    cJSON_AddStringToObject(root, "active", g_playset.name.c_str());
  }
  cJSON *arr = cJSON_AddArrayToObject(root, "playsets");
  for (const content::store::Summary &s : stored) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", s.name.c_str());
    cJSON_AddNumberToObject(o, "channels", static_cast<double>(s.channels));
    cJSON_AddItemToArray(arr, o);
  }
  if (!listed) cJSON_AddStringToObject(root, "error", error.c_str());
  cJSON *builtins = cJSON_AddArrayToObject(root, "builtins");
  for (size_t i = 0; i < content::kBuiltinCount; ++i) {
    const content::Builtin b = static_cast<content::Builtin>(i);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", content::builtin_name(b));
    bool enabled = false;
    const char *reason = "";
    switch (b) {
      case content::Builtin::Promoted:
        enabled = true;  // anonymous; plays from its cache when offline
        reason = online ? "" : "offline";
        break;
      case content::Builtin::All:
      case content::Builtin::Followed:
        enabled = paired;
        reason = paired ? "" : "needs pairing";
        break;
      case content::Builtin::Local:
        enabled = storage::mounted();
        reason = enabled ? "" : "no card";
        break;
    }
    cJSON_AddBoolToObject(o, "enabled", enabled);
    cJSON_AddStringToObject(o, "reason", reason);
    cJSON_AddItemToArray(builtins, o);
  }
  cJSON_AddNumberToObject(root, "max_playsets", static_cast<double>(content::kMaxPlaysets));
  return root;
}

std::string active_playset_name() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_playset.name;
}

}  // namespace p64::show

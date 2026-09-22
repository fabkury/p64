// The show's shell: the command queue drained by the main task, the loader's results,
// the event subscriptions, the public API and the device's ShowEnv. The show's logic is
// the core (show_core.cpp, host-tested); this file only connects it to the firmware.
#include "show.hpp"

#include <cstdio>
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
#include "p64/content/local_index.hpp"
#include "p64/content/playset_store.hpp"
#include "p64/content/psram.hpp"
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
#include "show_core.hpp"
#include "status_screens.hpp"

namespace p64::show {
namespace {

constexpr const char *TAG = "show";
constexpr const char *kActiveKey = "playset";

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

template <typename T, typename... Args>
std::shared_ptr<T> psram_shared(Args &&...args) {
  return std::allocate_shared<T>(content::PsramAllocator<T>(), std::forward<Args>(args)...);
}

// The core runs on the main task; g_mutex makes its state readable by the API's tasks.
std::mutex g_mutex;
QueueHandle_t g_commands = nullptr;
playback::Player *g_player = nullptr;
playback::Renderer *g_renderer = nullptr;
gfx::Frame *g_scratch = nullptr;  // PSRAM; status screens are drawn here

// The world, as the device provides it.
class DeviceEnv : public ShowEnv {
 public:
  int64_t now_us() override { return esp_timer_get_time(); }
  uint32_t random() override { return esp_random(); }
  std::shared_ptr<const system::Settings> settings() override { return system::settings_view(); }
  void persist_main_state(system::MainState state) override {
    system::settings_update([state](system::Settings &s) { s.main_state = state; });
  }
  void persist_active_playset(const std::string &name) override { system::state::set(kActiveKey, name); }
  bool load_playset(const std::string &name, content::Playset &out, std::string &error) override {
    return content::store::load(name, out, error);
  }
  void play(std::shared_ptr<playback::FrameSource> source) override { g_player->play(std::move(source)); }
  std::shared_ptr<playback::FrameSource> static_frame(const char *name, const gfx::Frame &frame) override {
    return psram_shared<playback::StaticSource>(name, frame);
  }
  std::shared_ptr<playback::FrameSource> widget(system::WidgetKind kind) override { return widgets::make(kind); }
  const char *widget_name(system::WidgetKind kind) override { return widgets::widget_name(kind); }
  std::shared_ptr<playback::FrameSource> stream_source() override { return stream::source(); }
  void stream_wake() override { stream::wake(); }
  stream::Status stream_status() override { return stream::status(); }
  RenderTotals render_totals() override {
    RenderTotals t;
    if (g_renderer) {
      const playback::Renderer::Stats r = g_renderer->totals();
      t.frames = r.frames;
      t.late = r.late;
      t.skipped = r.skipped;
    }
    return t;
  }
  uint32_t load(const std::string &path, gfx::Rgb background) override { return loader::load(path, background); }
  void scan(uint32_t generation, const content::Playset &playset) override { loader::scan(generation, playset); }
  bool card_mounted() override { return storage::mounted(); }
  std::string storage_root() override { return storage::root(); }
  std::string animations_dir() override { return storage::animations_dir(); }
  std::string downloads_dir() override { return storage::downloads_dir(); }
  makapix::Status makapix_status() override { return makapix::status(); }
  bool makapix_paired() override { return makapix::paired(); }
  void makapix_set_active_channels(const std::vector<makapix::ChannelRef> &refs) override {
    makapix::set_active_channels(refs);
  }
  bool makapix_snapshot(const makapix::ChannelRef &ref, makapix::ChannelSnapshot &out) override {
    return makapix::snapshot(ref, out);
  }
  std::string makapix_artwork_path(const content::MakapixEntry &entry) override { return makapix::artwork_path(entry); }
  void makapix_note_shown(int32_t post_id, const makapix::ChannelRef *channel, bool play_this) override {
    makapix::note_shown(post_id, channel, play_this);
  }
  void makapix_note_hidden() override { makapix::note_hidden(); }
  void makapix_note_load_failed(const content::MakapixEntry &entry, bool missing) override {
    makapix::note_load_failed(entry, missing);
  }
  bool makapix_play_followed(std::string &error) override { return makapix::play_followed(error); }
  net::wifi::Status wifi_status() override { return net::wifi::status(); }
  void playback_swapped(int32_t history_position) override {
    system::publish(system::Event::PlaybackSwapped, history_position);
  }
  void notify_web() override { web::notify(); }
  void vlog(char level, const char *format, va_list args) override {
    char line[256];
    vsnprintf(line, sizeof line, format, args);
    switch (level) {
      case 'E': ESP_LOGE(TAG, "%s", line); break;
      case 'W': ESP_LOGW(TAG, "%s", line); break;
      case 'D': ESP_LOGD(TAG, "%s", line); break;
      default: ESP_LOGI(TAG, "%s", line); break;
    }
  }
};

DeviceEnv g_env;

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

void handle(Command &c) {
  switch (c.type) {
    case Cmd::Next: core::next(); break;
    case Cmd::Previous: core::previous(); break;
    case Cmd::GoTo: core::go_to(c.number); break;
    case Cmd::Pause: core::pause(); break;
    case Cmd::Resume: core::resume(); break;
    case Cmd::ResetTimer: core::reset_timer(); break;
    case Cmd::Refresh: core::refresh(); break;
    case Cmd::PlayFile:
      if (c.text) core::play_file(*c.text, -1, "");
      break;
    case Cmd::PlayDownloaded:
      if (c.text) core::play_file(*c.text, static_cast<int32_t>(c.number), c.text2 ? *c.text2 : "");
      break;
    case Cmd::Activate:
      if (c.text) core::activate(*c.text);
      break;
    case Cmd::ActivateTransient:
      if (c.playset) core::activate_transient(*c.playset);
      break;
    case Cmd::Loaded: core::on_loaded(std::unique_ptr<loader::LoadResult>(c.load)); break;
    case Cmd::Scanned: core::on_scanned(std::unique_ptr<loader::ScanResult>(c.scan)); break;
    case Cmd::CardChanged: core::card_changed(); break;
    case Cmd::FilesChanged: core::files_changed(); break;
    case Cmd::Settings: core::settings_changed(); break;
    case Cmd::MakapixChanged: core::makapix_changed(); break;
    case Cmd::MakapixState: core::makapix_state(static_cast<makapix::State>(c.number)); break;
    case Cmd::WifiConnected: core::wifi_connected(); break;
    case Cmd::StreamStarted: core::stream_started(); break;
    case Cmd::StreamEnded: core::stream_ended(); break;
  }
  delete c.text;
  delete c.text2;
  delete c.playset;
}

TickType_t wait_ticks() {
  const TickType_t ticks = pdMS_TO_TICKS(core::wait_us() / 1000 + 1);
  return ticks ? ticks : 1;
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
  core::init(g_env, *g_scratch);
  // The boot animation holds the panel until it has run its course (spec 15.1).
  if (boot_animation_ms > 0) {
    core::boot(psram_shared<BootSource>(boot_animation_ms), boot_animation_ms);
  } else {
    status_screens::black(*g_scratch);
    core::boot(psram_shared<playback::StaticSource>("boot", *g_scratch), 0);
  }
  g_player->set_overlay(playback::Player::Overlay{
      [] { return core::overlay_allowed() ? widgets::overlay_key() : 0u; }, widgets::draw_overlay});
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
  std::string name;
  system::state::get(kActiveKey, name);
  std::lock_guard<std::mutex> lock(g_mutex);
  core::restore(name);
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
    core::tick();
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
  return core::is_paused();
}

int32_t current_post_id() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return core::current_post_id();
}

cJSON *status_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return core::status_json();
}

cJSON *channels_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return core::channels_json();
}

cJSON *history_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return core::history_json();
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
    cJSON_AddStringToObject(root, "active", core::active_playset_name().c_str());
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
  return core::active_playset_name();
}

}  // namespace p64::show

// The show's shell: the command queue drained by the main task, the loader's results,
// the event subscriptions, the public API and the device's ShowEnv. The show's logic is
// the core (show_core.cpp, host-tested); this file only connects it to the firmware.
#include "show.hpp"

#include <atomic>
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

#include "p64/intro/intro.hpp"
#include "loader.hpp"
#include "p64/content/local_index.hpp"
#include "p64/content/playset_store.hpp"
#include "p64/content/psram.hpp"
#include "p64/makapix/makapix.hpp"
#include "p64/net/wifi.hpp"
#include "p64/ota/ota.hpp"
#include "p64/playback/frame_source.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"
#include "p64/system/state_store.hpp"
#include "p64/stream/stream.hpp"
#include "p64/web/web.hpp"
#include "p64/inputs/knob_rules.hpp"
#include "p64/widgets/notice.hpp"
#include "p64/widgets/widgets.hpp"
#include "show_core.hpp"
#include "status_screens.hpp"

namespace p64::show {
namespace {
// The brightness notice (p095): when the knob last turned past its maximum (0 = never).
std::atomic<int64_t> g_notice_hit_us{0};

uint8_t notice_alpha_now() {
  const int64_t hit = g_notice_hit_us.load();
  if (hit == 0 || !core::notice_allowed()) return 0;
  return inputs::notice_alpha(esp_timer_get_time() - hit);
}


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
  Interlude,
  PlayIntro,
  PlayFile,
  PlayDownloaded,
  Activate,
  ActivateTransient,
  Loaded,
  Scanned,
  CardChanged,
  FilesChanged,
  Settings,
  ProviderChanged,
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
  std::string *text3;
  loader::LoadResult *load;
  loader::ScanResult *scan;
  content::Playset *playset;
};

template <typename T, typename... Args>
std::shared_ptr<T> psram_shared(Args &&...args) {
  return std::allocate_shared<T>(content::PsramAllocator<T>(), std::forward<Args>(args)...);
}

// The boot animation as a source (p101, spec 15.1): one of the intro animations, paced
// at intro::kFrameMs over `duration_ms` from the source's creation, then the finished
// logo held (a frame every 100 ms) until the first artwork replaces it. The context and
// the animations' working memory live with the source, in PSRAM like the source itself.
class IntroSource : public playback::FrameSource {
 public:
  IntroSource(int index, uint32_t duration_ms, uint32_t seed, gfx::Rgb background)
      : index_(index), duration_ms_(duration_ms), t0_(esp_timer_get_time()) {
    ctx_.seed = seed;
    ctx_.background = background;
    ctx_.scratch = scratch_;
    name_ = std::string("intro:") + (intro::name(index) ? intro::name(index) : "?");
  }
  const std::string &name() const override { return name_; }
  bool next_frame(gfx::Frame &out, uint32_t &delay_ms, int64_t due_us) override {
    const int64_t at = due_us ? due_us : esp_timer_get_time();
    const float elapsed_ms = at > t0_ ? static_cast<float>(at - t0_) / 1000.0f : 0.0f;
    const float t = duration_ms_ ? elapsed_ms / static_cast<float>(duration_ms_) : 1.0f;
    intro::render(index_, out, ctx_, t);
    delay_ms = t >= intro::kHoldFrom ? 100 : intro::kFrameMs;
    return true;
  }
  bool is_static() const override { return false; }

 private:
  std::string name_;
  int index_;
  uint32_t duration_ms_;
  int64_t t0_;
  intro::Context ctx_;
  uint8_t scratch_[intro::kScratchBytes];
};

// The animation to play: the setting's name, or a random one (p101).
int pick_intro(const std::string &setting) {
  const int forced = intro::find(setting.c_str());
  if (forced >= 0) return forced;
  return static_cast<int>(esp_random() % static_cast<uint32_t>(intro::count()));
}

std::shared_ptr<playback::FrameSource> make_intro(int index, uint32_t duration_ms) {
  return psram_shared<IntroSource>(index, duration_ms, esp_random(), system::settings().background);
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
  std::shared_ptr<playback::FrameSource> widget(system::WidgetKind kind, int face) override {
    return widgets::make(kind, face);
  }
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
  bool makapix_play_followed(std::string &error) override { return makapix::play_followed(error); }
  net::wifi::Status wifi_status() override { return net::wifi::status(); }
  UpdateState update_state() override {
    const ota::Status o = ota::status();
    UpdateState u;
    using P = UpdateState::Phase;
    switch (o.state) {
      case ota::State::Downloading: u.phase = P::Downloading; break;
      case ota::State::Verifying: u.phase = P::Verifying; break;
      case ota::State::ReadyToReboot: u.phase = P::Ready; break;
      case ota::State::Error: u.phase = P::Failed; break;
      default: u.phase = P::None; break;
    }
    u.version = o.available_version;
    if (o.image_size > 0) u.percent = static_cast<int>(static_cast<uint64_t>(o.bytes_read) * 100 / o.image_size);
    u.error = o.error;
    return u;
  }
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
          loader::ScanResult *scan = nullptr, std::string *text2 = nullptr, content::Playset *playset = nullptr,
          std::string *text3 = nullptr) {
  Command c{type, number, text, text2, text3, load, scan, playset};
  if (!g_commands || xQueueSend(g_commands, &c, pdMS_TO_TICKS(500)) != pdTRUE) {
    ESP_LOGW(TAG, "command queue full; dropped command %d", static_cast<int>(type));
    delete text;
    delete text2;
    delete text3;
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
    case Cmd::Interlude: core::interlude(static_cast<system::WidgetKind>(c.number)); break;
    case Cmd::PlayIntro: {
      // One slot of the configured length (3 s when the boot animation is off).
      const uint16_t setting = system::settings().boot_animation_ms;
      const uint32_t ms = setting ? setting : 3000;
      core::play_intro(make_intro(static_cast<int>(c.number), ms), ms, intro::name(static_cast<int>(c.number)));
      break;
    }
    case Cmd::PlayFile:
      if (c.text) core::play_file(*c.text, "", -1, "");
      break;
    case Cmd::PlayDownloaded:
      if (c.text) {
        core::play_file(*c.text, c.text3 ? *c.text3 : "", static_cast<int32_t>(c.number), c.text2 ? *c.text2 : "");
      }
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
    case Cmd::ProviderChanged: core::provider_changed(); break;
    case Cmd::MakapixState: core::makapix_state(static_cast<makapix::State>(c.number)); break;
    case Cmd::WifiConnected: core::wifi_connected(); break;
    case Cmd::StreamStarted: core::stream_started(); break;
    case Cmd::StreamEnded: core::stream_ended(); break;
  }
  delete c.text;
  delete c.text2;
  delete c.text3;
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
  // The boot animation holds the panel until it has run its course (spec 15.1): one of
  // the intro animations, the setting's or a random one (p101).
  if (boot_animation_ms > 0) {
    const int pick = pick_intro(system::settings().boot_animation);
    ESP_LOGI(TAG, "boot animation: %s for %u ms", intro::name(pick), static_cast<unsigned>(boot_animation_ms));
    core::boot(make_intro(pick, boot_animation_ms), boot_animation_ms, intro::name(pick));
  } else {
    status_screens::black(*g_scratch);
    core::boot(psram_shared<playback::StaticSource>("boot", *g_scratch), 0);
  }
  // The clock overlay (spec 6.1) and the brightness notice (p095) share the player's
  // overlay: the key folds both so a change in either re-emits a static frame, and 0
  // still means nothing to draw (the fold is a bijection of the clock key, so a clock
  // key alone never folds to 0).
  g_player->set_overlay(playback::Player::Overlay{
      [] {
        const uint32_t clock = core::overlay_allowed() ? widgets::overlay_key() : 0u;
        return clock * 2654435761u + notice_alpha_now();
      },
      [](gfx::Frame &f) {
        widgets::draw_overlay(f);
        widgets::notice::draw_sun(f, notice_alpha_now());
      }});
  system::subscribe(system::Event::CardMounted, [](const system::Message &) { send(Cmd::CardChanged); });
  system::subscribe(system::Event::CardFailed, [](const system::Message &) { send(Cmd::CardChanged); });
  system::subscribe(system::Event::LocalFilesChanged, [](const system::Message &) { send(Cmd::FilesChanged); });
  system::subscribe(system::Event::SettingsChanged, [](const system::Message &) { send(Cmd::Settings); });
  system::subscribe(system::Event::ProviderChannelChanged, [](const system::Message &) { send(Cmd::ProviderChanged); });
  system::subscribe(system::Event::MakapixStateChanged,
                    [](const system::Message &m) { send(Cmd::MakapixState, static_cast<uint32_t>(m.arg)); });
  system::subscribe(system::Event::WifiConnected, [](const system::Message &) { send(Cmd::WifiConnected); });
  system::subscribe(system::Event::WifiDisconnected, [](const system::Message &) { send(Cmd::ProviderChanged); });
  system::subscribe(system::Event::TimeSynced, [](const system::Message &) { send(Cmd::ProviderChanged); });
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
void notice_brightness_max() { g_notice_hit_us = esp_timer_get_time(); }

void set_paused(bool paused) { send(paused ? Cmd::Pause : Cmd::Resume); }
void reset_timer() { send(Cmd::ResetTimer); }
void refresh() { send(Cmd::Refresh); }

bool interlude(const std::string &widget, std::string &error) {
  static const char *const kNames[] = {"clock", "weather", "temperature", "air"};
  int kind = -1;
  for (int i = 0; i < system::kWidgetKindCount; ++i) {
    if (widget == kNames[i]) kind = i;
  }
  if (kind < 0) {
    error = "widget must be clock, weather, temperature or air";
    return false;
  }
  if (system::settings().main_state != system::MainState::AnimationShow) {
    error = "interludes happen inside the Animation show";
    return false;
  }
  send(Cmd::Interlude, static_cast<uint32_t>(kind));
  return true;
}

bool play_intro(std::string &name, std::string &error) {
  int index = -1;
  if (name == "random") {
    index = pick_intro(name);
    name = intro::name(index);
  } else {
    index = intro::find(name.c_str());
  }
  if (index < 0) {
    error = "unknown animation name";
    return false;
  }
  if (system::settings().main_state != system::MainState::AnimationShow) {
    error = "intro animations play inside the Animation show";
    return false;
  }
  send(Cmd::PlayIntro, static_cast<uint32_t>(index));
  return true;
}

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

void play_downloaded(const std::string &path, const std::string &provider, int32_t item_id, const std::string &name) {
  send(Cmd::PlayDownloaded, static_cast<uint32_t>(item_id), new std::string(path), nullptr, nullptr,
       new std::string(name), nullptr, new std::string(provider));
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

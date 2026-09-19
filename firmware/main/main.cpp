// p64 -- firmware entry point.
//
// Milestone M4: the API and web UI on top of M3. The main task runs a small show
// controller (a command queue: next, play this file) that the API drives; the state
// machine and the content model replace it with M5.

#include <cinttypes>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "boot_animation.hpp"
#include "p64/display/display.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/net/clock.hpp"
#include "p64/net/http_server.hpp"
#include "p64/net/setup_portal.hpp"
#include "p64/net/wifi.hpp"
#include "p64/playback/artwork.hpp"
#include "p64/playback/frame_queue.hpp"
#include "p64/playback/player.hpp"
#include "p64/playback/renderer.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/log_ring.hpp"
#include "p64/system/settings.hpp"
#include "p64/web/web.hpp"

namespace {

constexpr const char *TAG = "p64";
constexpr uint32_t kFrameUs = 16667;

// The display and the queue indices stay in internal RAM (.bss); the three 12 KB ready
// frames, the boot scratch frame and the preview live in PSRAM (allocated in app_main),
// keeping internal RAM for Wi-Fi, TLS and task stacks.
p64::display::Display g_display;
p64::playback::ReadySlot *g_slots = nullptr;
p64::playback::FrameQueue *g_queue = nullptr;
p64::playback::Player g_player;
p64::playback::Renderer g_renderer;
p64::gfx::Frame *g_scratch = nullptr;

template <typename T>
T *psram_new() {
  void *mem = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) mem = malloc(sizeof(T));
  return mem ? new (mem) T() : nullptr;
}

// --- show controller (M4 stand-in for the state machine) ---------------------------

enum class Command : uint8_t { Next = 0, Play = 1 };

struct ShowCommand {
  Command type;
  std::string *path;  // Play: owned by the receiver
};

QueueHandle_t g_commands = nullptr;
std::vector<std::string> g_files;
int64_t g_artwork_since_us = 0;

void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS needs erasing (%s)", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

void mark_image_valid() {
  esp_ota_img_states_t state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGI(TAG, "first boot of this image: marking it valid");
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

// The picture settings the display applies directly; the panel mode goes through the
// renderer (a driver restart between frames).
void apply_display_settings(const p64::system::Settings &s) {
  g_display.set_rotation(static_cast<p64::gfx::Rotation>(s.rotation));
  g_display.set_gains(s.gain_r, s.gain_g, s.gain_b);
  g_display.set_brightness(std::min(s.brightness, s.brightness_ceiling));
  g_renderer.request_mode(s.panel_mode == p64::system::PanelMode::Photo ? p64::display::Mode::Photo
                                                                          : p64::display::Mode::Quality);
}

// The main task produces the boot animation into the queue before the player starts.
void play_boot_animation(uint32_t duration_ms) {
  const p64::BootAnimation boot;
  const int64_t t0 = esp_timer_get_time();
  while (true) {
    const uint32_t t_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    if (!boot.render(*g_scratch, t_ms, duration_ms)) break;
    p64::playback::ReadySlot *slot = g_queue->producer_slot();
    if (!slot) {
      vTaskDelay(1);
      continue;
    }
    slot->frame.copy_from(*g_scratch);
    slot->due_us = esp_timer_get_time();
    slot->delay_us = kFrameUs;
    slot->generation = 0;
    slot->first = false;
    g_queue->producer_publish();
  }
  ESP_LOGI(TAG, "boot animation done after %lld ms", static_cast<long long>((esp_timer_get_time() - t0) / 1000));
}

bool supported_extension(const std::string &ext) {
  return ext == ".gif" || ext == ".png" || ext == ".apng" || ext == ".webp" || ext == ".bmp";
}

// Artwork files in the card's animations folder, or in the card root when that folder
// is empty (the test card from the hardware tests keeps its files there).
std::vector<std::string> find_artworks() {
  std::vector<std::string> paths;
  for (const std::string &dir : {p64::storage::animations_dir(), std::string(p64::storage::mount_point())}) {
    for (const p64::storage::FileInfo &f : p64::storage::list(dir)) {
      if (!f.directory && supported_extension(p64::storage::extension_of(f.name))) paths.push_back(dir + "/" + f.name);
    }
    if (!paths.empty()) {
      ESP_LOGI(TAG, "%u artwork files in %s", static_cast<unsigned>(paths.size()), dir.c_str());
      break;
    }
  }
  return paths;
}

std::shared_ptr<p64::playback::Artwork> load_artwork(const std::string &path, p64::gfx::Rgb background,
                                                     std::string &error) {
  std::vector<uint8_t> bytes;
  const int64_t t0 = esp_timer_get_time();
  if (!p64::storage::read_file(path, bytes, p64::playback::kMaxFileBytes, error)) return nullptr;
  const int64_t t1 = esp_timer_get_time();
  auto art = std::make_shared<p64::playback::Artwork>();
  const std::string name = path.substr(path.rfind('/') + 1);
  if (!art->open(std::move(bytes), name, background, error)) return nullptr;
  ESP_LOGI(TAG, "playing %s: %s %dx%d, %u bytes, read in %lld ms, scaled %s%dx to %dx%d", name.c_str(),
           p64::decode::format_name(art->format()), art->info().width, art->info().height,
           static_cast<unsigned>(art->file_bytes()), static_cast<long long>((t1 - t0) / 1000),
           art->scaler().enlarging() ? "up " : "", art->scaler().factor(), art->scaler().out_w(),
           art->scaler().out_h());
  return art;
}

bool play_path(const std::string &path, std::string &error) {
  const p64::system::Settings s = p64::system::settings();
  auto art = load_artwork(path, s.background, error);
  if (!art) {
    ESP_LOGW(TAG, "%s: %s", path.c_str(), error.c_str());
    return false;
  }
  g_player.play(art);
  g_artwork_since_us = esp_timer_get_time();
  p64::system::publish(p64::system::Event::PlaybackSwapped);
  return true;
}

void play_random() {
  if (g_files.empty()) return;
  std::string error;
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (play_path(g_files[esp_random() % g_files.size()], error)) return;
  }
}

void send_command(Command type, const std::string *path) {
  ShowCommand c{type, path ? new std::string(*path) : nullptr};
  if (xQueueSend(g_commands, &c, pdMS_TO_TICKS(100)) != pdTRUE) delete c.path;
}

cJSON *playback_status() {
  cJSON *p = cJSON_CreateObject();
  cJSON_AddStringToObject(p, "state", "animation_show");
  if (auto art = g_player.current()) {
    cJSON *a = cJSON_AddObjectToObject(p, "artwork");
    cJSON_AddStringToObject(a, "name", art->name().c_str());
    cJSON_AddStringToObject(a, "format", p64::decode::format_name(art->format()));
    cJSON_AddNumberToObject(a, "width", art->info().width);
    cJSON_AddNumberToObject(a, "height", art->info().height);
    cJSON_AddNumberToObject(a, "bytes", static_cast<double>(art->file_bytes()));
    cJSON_AddBoolToObject(a, "animated", !art->is_static());
    cJSON_AddNumberToObject(a, "frames_decoded", art->frames_decoded());
    cJSON_AddNumberToObject(a, "since_s", static_cast<double>((esp_timer_get_time() - g_artwork_since_us) / 1000000));
  }
  const p64::playback::Renderer::Stats r = g_renderer.totals();
  cJSON_AddNumberToObject(p, "frames", r.frames);
  cJSON_AddNumberToObject(p, "late", r.late);
  cJSON_AddNumberToObject(p, "skipped", r.skipped);
  cJSON_AddNumberToObject(p, "files", static_cast<double>(g_files.size()));
  return p;
}

}  // namespace

extern "C" void app_main() {
  p64::system::logring::init(32 * 1024);
  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "p64 firmware %s (IDF %s), built %s %s", app->version, app->idf_ver, app->date, app->time);

  init_nvs();
  mark_image_valid();
  p64::system::event_bus_init();
  p64::system::settings_init();
  p64::system::Settings s = p64::system::settings();

  if (!g_display.begin()) {
    ESP_LOGE(TAG, "display failed to start; nothing to do");
    return;
  }
  g_display.set_rotation(static_cast<p64::gfx::Rotation>(s.rotation));
  g_display.set_gains(s.gain_r, s.gain_g, s.gain_b);
  g_display.set_brightness(std::min(s.brightness, s.brightness_ceiling));

  // Frames in PSRAM (see the globals above).
  void *slot_mem = heap_caps_malloc(sizeof(p64::playback::ReadySlot) * p64::playback::FrameQueue::kSlots,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  g_slots = new (slot_mem) p64::playback::ReadySlot[p64::playback::FrameQueue::kSlots];
  static p64::playback::FrameQueue queue(g_slots);
  g_queue = &queue;
  g_scratch = psram_new<p64::gfx::Frame>();
  g_renderer.start(g_display, *g_queue, &g_player);
  p64::system::subscribe(p64::system::Event::SettingsChanged,
                         [](const p64::system::Message &) { apply_display_settings(p64::system::settings()); });
  if (s.panel_mode == p64::system::PanelMode::Photo) g_renderer.request_mode(p64::display::Mode::Photo);

  play_boot_animation(s.boot_animation_ms);
  g_player.start(*g_queue);

  // Network: Wi-Fi start creates the TCP/IP stack; the HTTP server, the portal and the
  // API come right after; SNTP waits for the network on its own.
  p64::net::wifi::start(s.hostname());
  p64::net::http::start();
  p64::net::portal::init(p64::web::ui_handler);
  g_commands = xQueueCreate(8, sizeof(ShowCommand));
  p64::web::Hooks hooks;
  hooks.next = [] { send_command(Command::Next, nullptr); };
  hooks.play_file = [](const std::string &path, std::string &error) -> bool {
    // Validated here on the caller's task so the API can report a rejection; the
    // actual swap happens on the main task.
    std::vector<uint8_t> probe;
    if (!p64::storage::read_file(path, probe, p64::playback::kMaxFileBytes, error)) return false;
    const p64::decode::Format f = p64::decode::sniff(probe.data(), probe.size());
    if (f == p64::decode::Format::Unknown) {
      error = "unknown file format";
      return false;
    }
    send_command(Command::Play, &path);
    return true;
  };
  hooks.playback_status = playback_status;
  hooks.snapshot = [](p64::gfx::Frame &out) { return g_renderer.snapshot(out); };
  hooks.request_mode = [](p64::display::Mode m) { g_renderer.request_mode(m); };
  hooks.display = [] { return &g_display; };
  p64::web::init(hooks);
  p64::net::clock::start(s.ntp_server, s.timezone);
  p64::system::subscribe(p64::system::Event::SettingsChanged, [](const p64::system::Message &) {
    const p64::system::Settings now = p64::system::settings();
    p64::net::clock::set_timezone(now.timezone);
    p64::net::clock::set_ntp_server(now.ntp_server);
    p64::net::wifi::set_hostname(now.hostname());
  });

  if (p64::storage::mount()) {
    p64::system::publish(p64::system::Event::CardMounted);
  } else {
    p64::system::publish(p64::system::Event::CardFailed);
  }
  g_files = find_artworks();
  if (g_files.empty()) ESP_LOGW(TAG, "no artwork files on the card; the panel stays on the last boot frame");
  play_random();

  // The show loop: wait for a command or the auto-swap deadline, whichever comes first.
  while (true) {
    s = p64::system::settings();
    const int64_t interval_us = s.auto_swap_seconds == 0 ? 0 : static_cast<int64_t>(s.auto_swap_seconds) * 1000000;
    TickType_t wait = portMAX_DELAY;
    if (interval_us > 0) {
      const int64_t remaining = g_artwork_since_us + interval_us - esp_timer_get_time();
      wait = remaining <= 0 ? 0 : pdMS_TO_TICKS(std::min<int64_t>(remaining / 1000 + 1, 60000));
    }
    ShowCommand c{};
    if (xQueueReceive(g_commands, &c, wait) == pdTRUE) {
      if (c.type == Command::Play && c.path) {
        std::string error;
        play_path(*c.path, error);
      } else {
        play_random();
      }
      delete c.path;
      continue;
    }
    if (interval_us > 0 && esp_timer_get_time() - g_artwork_since_us >= interval_us) play_random();
  }
}

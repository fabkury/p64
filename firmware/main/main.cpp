// p64 -- firmware entry point.
//
// Milestone M6: app_main wires the display, the playback tasks, the network, the web
// layer and Makapix Club, mounts the card, restores the active playset and hands the
// main task to the show loop (main/show.cpp, the state machine).

#include <cinttypes>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "p64/display/display.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/makapix/makapix.hpp"
#include "p64/net/clock.hpp"
#include "p64/net/http_server.hpp"
#include "p64/net/setup_portal.hpp"
#include "p64/net/wifi.hpp"
#include "p64/playback/frame_queue.hpp"
#include "p64/playback/player.hpp"
#include "p64/playback/renderer.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/log_ring.hpp"
#include "p64/system/settings.hpp"
#include "p64/web/web.hpp"
#include "p64/inputs/inputs.hpp"
#include "p64/ota/ota.hpp"
#include "p64/stream/stream.hpp"
#include "p64/system/reliability.hpp"
#include "ops.hpp"
#include "p64/widgets/widgets.hpp"
#include "show.hpp"

namespace {

constexpr const char *TAG = "p64";

// The display and the queue indices stay in internal RAM (.bss); the three 12 KB ready
// frames and the preview live in PSRAM (allocated in app_main), keeping internal RAM
// for Wi-Fi, TLS and task stacks.
p64::display::Display g_display;
p64::playback::ReadySlot *g_slots = nullptr;
p64::playback::FrameQueue *g_queue = nullptr;
p64::playback::Player g_player;
p64::playback::Renderer g_renderer;

void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS needs erasing (%s)", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

// The picture settings the display applies directly; the panel mode goes through the
// renderer (switched between two frames).
void apply_display_settings(const p64::system::Settings &s) {
  // Rotation "auto": the IMU's resolved value once it has one; the setting is the fallback.
  const uint16_t rotation = s.rotation_auto && p64::inputs::auto_rotation_resolved() ? p64::inputs::auto_rotation() : s.rotation;
  g_display.set_rotation(static_cast<p64::gfx::Rotation>(rotation));
  g_display.set_gains(s.gain_r, s.gain_g, s.gain_b);
  g_display.set_brightness(p64::ops::effective_brightness(s));
  g_renderer.request_mode(s.panel_mode == p64::system::PanelMode::Photo ? p64::display::Mode::Photo
                                                                          : p64::display::Mode::Quality);
}

}  // namespace

// cJSON trees (the API's status and settings documents, several KB each, six at once
// when a browser opens a page) come from PSRAM, not the scarce internal heap.
void *psram_malloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}

extern "C" void app_main() {
  cJSON_Hooks json_hooks = {psram_malloc, free};
  cJSON_InitHooks(&json_hooks);
  p64::system::logring::init(32 * 1024);
  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "p64 firmware %s (IDF %s), built %s %s", app->version, app->idf_ver, app->date, app->time);
  // This task becomes the show loop. ESP-IDF starts app_main at priority 1, below every
  // other task on core 0; the architecture (section 2) puts the show loop at 5, level
  // with httpd, the event dispatcher and MQTT and above the fetcher, the loader and the
  // widgets' tasks, so a TLS handshake never holds the user's commands (review of
  // 2026-09-22, which found it still at 1).
  vTaskPrioritySet(nullptr, 5);

  init_nvs();
  p64::system::reliability::init();
  p64::system::event_bus_init();
  p64::system::settings_init();
  const p64::system::Settings s = p64::system::settings();

  if (!g_display.begin()) {
    ESP_LOGE(TAG, "display failed to start; nothing to do");
    return;
  }
  g_display.set_rotation(static_cast<p64::gfx::Rotation>(s.rotation));
  g_display.set_gains(s.gain_r, s.gain_g, s.gain_b);
  g_display.set_brightness(p64::ops::effective_brightness(s));

  // Frames in PSRAM (see the globals above).
  void *slot_mem = heap_caps_malloc(sizeof(p64::playback::ReadySlot) * p64::playback::FrameQueue::kSlots,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  g_slots = new (slot_mem) p64::playback::ReadySlot[p64::playback::FrameQueue::kSlots];
  static p64::playback::FrameQueue queue(g_slots);
  g_queue = &queue;
  g_renderer.start(g_display, *g_queue, &g_player);
  g_player.start(*g_queue);
  p64::system::subscribe(p64::system::Event::SettingsChanged,
                         [](const p64::system::Message &) { apply_display_settings(p64::system::settings()); });
  if (s.panel_mode == p64::system::PanelMode::Photo) g_renderer.request_mode(p64::display::Mode::Photo);

  // The show puts the boot animation up at once; the card mounts and the playset is
  // restored while it runs (spec 15.1).
  if (!p64::show::init(g_player, g_renderer, s.boot_animation_ms)) {
    ESP_LOGE(TAG, "show failed to start");
    return;
  }
  // BOOT held at power-on: the factory reset countdown (spec 9); returns at once otherwise.
  {
    void *mem = heap_caps_malloc(sizeof(p64::gfx::Frame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    auto *scratch = mem ? new (mem) p64::gfx::Frame() : new p64::gfx::Frame();
    p64::ops::check_boot_hold(g_player, *scratch);
  }

  // Network: Wi-Fi start creates the TCP/IP stack; the HTTP server, the portal and the
  // API come right after; SNTP waits for the network on its own.
  p64::net::wifi::start(s.hostname());
  p64::net::http::start();
  p64::net::portal::init(p64::web::ui_handler);
  p64::web::Hooks hooks;
  hooks.next = p64::show::next;
  hooks.previous = p64::show::previous;
  hooks.pause = p64::show::pause;
  hooks.resume = p64::show::resume;
  hooks.reset_timer = p64::show::reset_timer;
  hooks.refresh = p64::show::refresh;
  hooks.go_to = p64::show::go_to;
  hooks.play_file = p64::show::play_file;
  hooks.play_post = p64::makapix::play_post;
  hooks.play_url = p64::makapix::play_url;
  hooks.like = p64::makapix::like;
  hooks.activate_playset = p64::show::activate_playset;
  hooks.playback_status = p64::show::status_json;
  hooks.channels = p64::show::channels_json;
  hooks.history = p64::show::history_json;
  hooks.playsets = p64::show::playsets_json;
  hooks.snapshot = [](p64::gfx::Frame &out) { return g_renderer.snapshot(out); };
  hooks.request_mode = [](p64::display::Mode m) { g_renderer.request_mode(m); };
  hooks.display = [] { return &g_display; };
  hooks.night_active = p64::ops::night_active;
  hooks.factory_reset = [] { p64::ops::factory_reset(); };
  p64::web::init(hooks);
  p64::ops::start([] { apply_display_settings(p64::system::settings()); });
  p64::net::clock::start(s.ntp_server, s.timezone);
  p64::system::subscribe(p64::system::Event::SettingsChanged, [](const p64::system::Message &) {
    const p64::system::Settings now = p64::system::settings();
    p64::net::clock::set_timezone(now.timezone);
    p64::net::clock::set_ntp_server(now.ntp_server);
    p64::net::wifi::set_hostname(now.hostname());
  });

  // Makapix Club: commands from the site drive the show; downloads come back as
  // play-this requests; the Followed playset arrives as a transient activation.
  p64::makapix::Hooks mk;
  mk.next = p64::show::next;
  mk.previous = p64::show::previous;
  mk.set_paused = p64::show::set_paused;
  mk.set_brightness = [](uint8_t b) {
    p64::system::settings_update([b](p64::system::Settings &st) { st.brightness = b; });
  };
  mk.set_rotation = [](uint16_t r) {
    p64::system::settings_update([r](p64::system::Settings &st) { st.rotation = r; });
  };
  mk.play_artwork = [](const std::string &path, int32_t post_id, const std::string &name) {
    p64::show::play_downloaded(path, "makapix", post_id, name);
  };
  mk.play_playset = p64::show::activate_transient;
  mk.current_post_id = p64::show::current_post_id;
  mk.is_paused = p64::show::is_paused;

  if (p64::storage::mount()) {
    p64::system::publish(p64::system::Event::CardMounted);
  } else {
    p64::system::publish(p64::system::Event::CardFailed);
  }
  p64::makapix::start(mk);
  p64::widgets::start();
  if (!p64::stream::start()) ESP_LOGE(TAG, "stream listener failed to start");
  p64::inputs::Hooks in;
  in.next = p64::show::next;
  in.previous = p64::show::previous;
  in.rotation_changed = [] { apply_display_settings(p64::system::settings()); };
  p64::inputs::start(in);
  p64::ota::start();
  p64::show::restore();
  p64::show::run();
}

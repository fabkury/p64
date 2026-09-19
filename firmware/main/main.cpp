// p64 -- firmware entry point.
//
// Milestone M3: settings from NVS, the event bus, Wi-Fi with setup mode, the HTTP server
// with the setup portal, SNTP with the time zone table. The show still plays random
// card artworks at the auto-swap interval from the settings; the state machine and the
// content model come with M5.

#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
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
#include "p64/system/settings.hpp"

namespace {

constexpr const char *TAG = "p64";
constexpr uint32_t kFrameUs = 16667;

// Off the task stacks and in internal RAM (.bss): the queue holds three 12 KB frames.
p64::display::Display g_display;
p64::playback::FrameQueue g_queue;
p64::playback::Player g_player;
p64::playback::Renderer g_renderer;
p64::gfx::Frame g_scratch;

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

// The picture settings the display applies directly. Panel mode switching goes through
// the renderer (a driver restart between frames) once the API can request it.
void apply_display_settings(const p64::system::Settings &s) {
  g_display.set_rotation(static_cast<p64::gfx::Rotation>(s.rotation));
  g_display.set_gains(s.gain_r, s.gain_g, s.gain_b);
  g_display.set_brightness(std::min(s.brightness, s.brightness_ceiling));
}

// The main task produces the boot animation into the queue before the player starts.
void play_boot_animation(uint32_t duration_ms) {
  const p64::BootAnimation boot;
  const int64_t t0 = esp_timer_get_time();
  while (true) {
    const uint32_t t_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    if (!boot.render(g_scratch, t_ms, duration_ms)) break;
    p64::playback::ReadySlot *slot = g_queue.producer_slot();
    if (!slot) {
      vTaskDelay(1);
      continue;
    }
    slot->frame.copy_from(g_scratch);
    slot->due_us = esp_timer_get_time();
    slot->delay_us = kFrameUs;
    slot->generation = 0;
    slot->first = false;
    g_queue.producer_publish();
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

std::shared_ptr<p64::playback::Artwork> load_artwork(const std::string &path, p64::gfx::Rgb background) {
  std::vector<uint8_t> bytes;
  std::string error;
  const int64_t t0 = esp_timer_get_time();
  if (!p64::storage::read_file(path, bytes, p64::playback::kMaxFileBytes, error)) {
    ESP_LOGW(TAG, "%s: %s", path.c_str(), error.c_str());
    return nullptr;
  }
  const int64_t t1 = esp_timer_get_time();
  auto art = std::make_shared<p64::playback::Artwork>();
  const std::string name = path.substr(path.rfind('/') + 1);
  if (!art->open(std::move(bytes), name, background, error)) {
    ESP_LOGW(TAG, "%s: rejected: %s", name.c_str(), error.c_str());
    return nullptr;
  }
  ESP_LOGI(TAG, "playing %s: %s %dx%d, %u bytes, read in %lld ms, scaled %s%dx to %dx%d", name.c_str(),
           p64::decode::format_name(art->format()), art->info().width, art->info().height,
           static_cast<unsigned>(art->file_bytes()), static_cast<long long>((t1 - t0) / 1000),
           art->scaler().enlarging() ? "up " : "", art->scaler().factor(), art->scaler().out_w(),
           art->scaler().out_h());
  return art;
}

}  // namespace

extern "C" void app_main() {
  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "p64 firmware %s (IDF %s), built %s %s", app->version, app->idf_ver, app->date, app->time);

  init_nvs();
  mark_image_valid();
  p64::system::event_bus_init();
  p64::system::settings_init();
  p64::system::Settings s = p64::system::settings();
  ESP_LOGI(TAG, "settings: %s", s.to_json().c_str());

  if (!g_display.begin()) {
    ESP_LOGE(TAG, "display failed to start; nothing to do");
    return;
  }
  apply_display_settings(s);
  p64::system::subscribe(p64::system::Event::SettingsChanged,
                         [](const p64::system::Message &) { apply_display_settings(p64::system::settings()); });
  g_renderer.start(g_display, g_queue, &g_player);

  play_boot_animation(s.boot_animation_ms);
  g_player.start(g_queue);

  // Network: Wi-Fi start creates the TCP/IP stack, so the HTTP server (and the portal
  // routes on it) come right after; SNTP waits for the network on its own.
  p64::net::wifi::start(s.hostname());
  p64::net::http::start();
  p64::net::portal::init(nullptr);
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
  std::vector<std::string> files = find_artworks();
  if (files.empty()) {
    ESP_LOGW(TAG, "no artwork files on the card; the panel stays on the last boot frame");
    return;
  }
  while (true) {
    s = p64::system::settings();
    const std::string &path = files[esp_random() % files.size()];
    if (auto art = load_artwork(path, s.background)) g_player.play(art);
    const uint32_t seconds = s.auto_swap_seconds == 0 ? 3600 : s.auto_swap_seconds;
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
  }
}

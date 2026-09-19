#include "ops.hpp"

#include <atomic>
#include <ctime>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p64/content/psram.hpp"
#include "p64/makapix/makapix.hpp"
#include "p64/net/clock.hpp"
#include "p64/net/wifi.hpp"
#include "p64/playback/frame_source.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/night.hpp"
#include "p64/system/reliability.hpp"
#include "p64/system/state_store.hpp"
#include "p64/web/web.hpp"
#include "sdkconfig.h"
#include "status_screens.hpp"

namespace p64::ops {
namespace {

constexpr const char *TAG = "ops";
constexpr int64_t kHoldUs = 10LL * 1000 * 1000;
constexpr int64_t kCountdownFromUs = 7LL * 1000 * 1000;
constexpr uint64_t kConfirmAfterUs = 30ULL * 1000 * 1000;
constexpr uint64_t kNightPeriodUs = 15ULL * 1000 * 1000;

std::function<void()> g_reapply;
std::atomic<bool> g_night_active{false};
std::atomic<int> g_last_applied{-1};
esp_timer_handle_t g_night_timer = nullptr;
esp_timer_handle_t g_confirm_timer = nullptr;

int local_minutes() {
  struct tm t;
  if (!net::clock::local_time(t)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

void night_tick(void *) {
  const system::Settings s = system::settings();
  const int b = effective_brightness(s);
  if (b != g_last_applied.load()) {
    ESP_LOGI(TAG, "effective brightness %d (%s)", b, g_night_active.load() ? "night window" : "user setting");
    if (g_reapply) g_reapply();
  }
}

void confirm_tick(void *) { system::reliability::mark_image_valid(); }

}  // namespace

uint8_t effective_brightness(const system::Settings &s) {
  bool night = false;
  const uint8_t b = system::night::effective_brightness(s, local_minutes(), night);
  g_night_active = night;
  g_last_applied = b;
  return b;
}

bool night_active() { return g_night_active.load(); }

void start(std::function<void()> reapply_display) {
  g_reapply = std::move(reapply_display);
  const esp_timer_create_args_t night_args = {night_tick, nullptr, ESP_TIMER_TASK, "night", false};
  esp_timer_create(&night_args, &g_night_timer);
  esp_timer_start_periodic(g_night_timer, kNightPeriodUs);
  system::subscribe(system::Event::TimeSynced, [](const system::Message &) { night_tick(nullptr); });
  if (system::reliability::image_pending_verify()) {
    // A fresh image proves itself by running for 30 s with the show loop up; a crash
    // before that reboots into the previous image (bootloader rollback).
    const esp_timer_create_args_t confirm_args = {confirm_tick, nullptr, ESP_TIMER_TASK, "confirm", false};
    esp_timer_create(&confirm_args, &g_confirm_timer);
    esp_timer_start_once(g_confirm_timer, kConfirmAfterUs);
  }
}

void check_boot_hold(playback::Player &player, gfx::Frame &scratch) {
  const auto pin = static_cast<gpio_num_t>(CONFIG_P64_BOOT_GPIO);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&cfg);
  if (gpio_get_level(pin) != 0) return;
  ESP_LOGW(TAG, "BOOT held at power-on: factory reset in 10 s unless released");
  const int64_t t0 = esp_timer_get_time();
  int shown = -1;
  while (gpio_get_level(pin) == 0) {
    const int64_t held = esp_timer_get_time() - t0;
    if (held >= kHoldUs) {
      status_screens::countdown(scratch, 0);
      player.play(std::allocate_shared<playback::StaticSource>(content::PsramAllocator<playback::StaticSource>(), "reset", scratch));
      vTaskDelay(pdMS_TO_TICKS(300));
      factory_reset();
    }
    if (held >= kCountdownFromUs) {
      const int n = static_cast<int>((kHoldUs - held) / 1000000) + 1;  // 3, 2, 1
      if (n != shown) {
        shown = n;
        status_screens::countdown(scratch, n);
        player.play(std::allocate_shared<playback::StaticSource>(content::PsramAllocator<playback::StaticSource>(), "countdown", scratch));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  ESP_LOGI(TAG, "BOOT released after %lld ms; normal boot", (esp_timer_get_time() - t0) / 1000);
}

[[noreturn]] void factory_reset() {
  ESP_LOGW(TAG, "factory reset: erasing settings, state, Wi-Fi, Makapix and PIN");
  std::string error;
  makapix::unpair(error);
  net::wifi::erase_credentials();
  system::state::erase_all();
  system::settings_reset();
  web::auth_erase();
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_restart();
}

}  // namespace p64::ops

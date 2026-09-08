// p64 -- bring-up test firmware for the Waveshare ESP32-S3-RGB-Matrix + P2 64x64 panel.
//
// Cycles through: bouncing ball (20 s) -> white fade, linear (40 s) -> white fade,
// perceptual (40 s) -> square hue wheel (20 s) -> repeat. Press BOOT to skip ahead.

#include <cinttypes>
#include <cstddef>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button.hpp"
#include "display.hpp"
#include "scene.hpp"
#include "scenes/ball.hpp"
#include "scenes/fade.hpp"
#include "scenes/hue_wheel.hpp"

namespace {

constexpr const char *TAG = "p64";
constexpr uint32_t kFramePeriodMs = 20;  // 50 frames per second

// Static so the 12 KB frame and the hue tables stay off the main task's stack.
p64::Frame g_frame;
p64::BallScene g_ball;
p64::FadeScene g_fade_linear{p64::FadeScene::Curve::Linear};
p64::FadeScene g_fade_perceptual{p64::FadeScene::Curve::Perceptual};
p64::HueWheelScene g_hue_wheel;
p64::Scene *const g_scenes[] = {&g_ball, &g_fade_linear, &g_fade_perceptual, &g_hue_wheel};
constexpr size_t kSceneCount = sizeof(g_scenes) / sizeof(g_scenes[0]);

// Runs one scene until it times out or BOOT is pressed.
void run_scene(p64::Scene &scene, p64::Display &display, p64::Button &boot) {
  scene.enter(display, g_frame);
  const int64_t t0 = esp_timer_get_time();
  int64_t t_prev = t0;
  TickType_t wake = xTaskGetTickCount();
  while (true) {
    const int64_t now = esp_timer_get_time();
    const uint32_t t_ms = static_cast<uint32_t>((now - t0) / 1000);
    if (t_ms >= scene.duration_ms()) return;
    if (boot.poll()) {
      ESP_LOGI(TAG, "BOOT pressed, skipping ahead");
      return;
    }
    const float dt_s = static_cast<float>(now - t_prev) / 1e6f;
    t_prev = now;
    if (scene.render(display, g_frame, t_ms, dt_s)) display.present(g_frame);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kFramePeriodMs));
  }
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(TAG, "p64 bring-up firmware, built " __DATE__ " " __TIME__);
  ESP_LOGI(TAG, "brightness cap %u/255", static_cast<unsigned>(p64::max_brightness()));

  static p64::Display display;
  if (!display.begin()) {
    ESP_LOGE(TAG, "display failed to start; nothing to do");
    return;
  }
  static p64::Button boot;
  boot.begin();

  for (size_t i = 0;; i = (i + 1) % kSceneCount) {
    p64::Scene &scene = *g_scenes[i];
    ESP_LOGI(TAG, "phase %u/%u: %s, %" PRIu32 " s (BOOT skips)", static_cast<unsigned>(i + 1),
             static_cast<unsigned>(kSceneCount), scene.name(), scene.duration_ms() / 1000);
    run_scene(scene, display, boot);
  }
}

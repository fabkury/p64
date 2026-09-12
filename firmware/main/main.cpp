// p64 -- frame-rate test firmware for the Waveshare ESP32-S3-RGB-Matrix + P2 64x64 panel.
//
// Runs the bouncing-ball scene (hue-cycling ball, FPS counter top-right) in 10 s
// rounds, logging frame statistics after each round. Press BOOT to restart a round.
//
// Frame pacing: a scene renders the next frame into RAM right after the previous one
// was flipped in, so rendering overlaps the panel's buffer switch; the loop then waits
// for the DMA to reach the frame boundary (see display.hpp) before copying the frame
// into the freed back buffer. That locks rendering to the panel refresh: one new frame
// per refresh, no tearing.

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

namespace {

constexpr const char *TAG = "p64";

// Static so the 12 KB frame stays off the main task's stack.
p64::Frame g_frame;
p64::BallScene g_ball;
p64::Scene *const g_scenes[] = {&g_ball};
constexpr size_t kSceneCount = sizeof(g_scenes) / sizeof(g_scenes[0]);

// Frames presented per second, measured over ~0.5 s windows.
class FpsMeter {
 public:
  void reset(int64_t now_us) {
    window_start_us_ = now_us;
    frames_ = 0;
    total_frames_ = 0;
    fps_ = 0.0f;
  }
  void tick(int64_t now_us) {
    ++frames_;
    ++total_frames_;
    const int64_t elapsed = now_us - window_start_us_;
    if (elapsed >= 500000) {
      fps_ = static_cast<float>(frames_) * 1e6f / static_cast<float>(elapsed);
      frames_ = 0;
      window_start_us_ = now_us;
    }
  }
  float fps() const { return fps_; }
  uint32_t total_frames() const { return total_frames_; }

 private:
  int64_t window_start_us_ = 0;
  uint32_t frames_ = 0;
  uint32_t total_frames_ = 0;
  float fps_ = 0.0f;
};

// Runs one scene until it times out or BOOT is pressed.
void run_scene(p64::Scene &scene, p64::Display &display, p64::Button &boot) {
  scene.enter(display, g_frame);
  const int64_t t0 = esp_timer_get_time();
  int64_t t_prev = t0;
  FpsMeter meter;
  meter.reset(t0);
  uint64_t render_us = 0;
  while (true) {
    const int64_t now = esp_timer_get_time();
    const uint32_t t_ms = static_cast<uint32_t>((now - t0) / 1000);
    if (t_ms >= scene.duration_ms()) break;
    if (boot.poll()) {
      ESP_LOGI(TAG, "BOOT pressed, skipping ahead");
      break;
    }
    const p64::FrameInfo info{t_ms, static_cast<float>(now - t_prev) / 1e6f, meter.fps()};
    t_prev = now;
    const bool dirty = scene.render(display, g_frame, info);
    render_us += static_cast<uint64_t>(esp_timer_get_time() - now);
    if (dirty) {
      display.wait_for_back_buffer();
      display.present(g_frame);
      meter.tick(esp_timer_get_time());
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));  // static scene: just keep polling the button
    }
  }
  const float seconds = static_cast<float>(esp_timer_get_time() - t0) / 1e6f;
  const p64::Display::Stats stats = display.take_stats();
  if (meter.total_frames() > 0) {
    const float n = static_cast<float>(meter.total_frames());
    ESP_LOGI(TAG, "%s: %" PRIu32 " frames in %.1f s = %.1f fps average", scene.name(), meter.total_frames(),
             seconds, n / seconds);
    ESP_LOGI(TAG, "  per frame: render %.2f ms, wait %.2f ms, copy %.2f ms; late flips %" PRIu32
                  ", sync timeouts %" PRIu32 " (%s)",
             static_cast<float>(render_us) / n / 1000.0f, static_cast<float>(stats.wait_us) / n / 1000.0f,
             static_cast<float>(stats.copy_us) / n / 1000.0f, stats.late_flips, stats.timeouts,
             display.dma_sync() ? "frame-locked to the DMA" : "timed fallback");
  }
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(TAG, "p64 frame-rate test firmware, built " __DATE__ " " __TIME__);
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
    ESP_LOGI(TAG, "round: %s, %" PRIu32 " s (BOOT restarts)", scene.name(), scene.duration_ms() / 1000);
    run_scene(scene, display, boot);
  }
}

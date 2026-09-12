// p64 -- firmware for the Waveshare ESP32-S3-RGB-Matrix + P2 64x64 panel.
//
// Starts on an embedded GIF (assets/gifs), then plays a random promoted Makapix Club
// artwork every 30 s, fetched in the background (embedded GIFs fill in when offline),
// at the GIFs' intended speed, with an NTP-synced clock top-left. Press BOOT to
// restart the scene. Menuconfig (menu "p64") can turn it back into the frame-rate test.
//
// Frame pacing: a scene renders the next frame into RAM right after the previous one
// was flipped in, so rendering overlaps the panel's buffer switch; the loop then waits
// for the DMA to reach the frame boundary (see display.hpp) before copying the frame
// into the freed back buffer. Frames are only presented when something changed.

#include <cinttypes>
#include <cstddef>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "button.hpp"
#include "display.hpp"
#include "net/clock.hpp"
#include "net/makapix.hpp"
#include "net/speedtest.hpp"
#include "net/wifi.hpp"
#include "scene.hpp"
#include "scenes/gif_show.hpp"

namespace {

constexpr const char *TAG = "p64";
constexpr int64_t kStatsIntervalUs = 10 * 1000 * 1000;

// Static so the frame and the scene's decoder state stay off the main task's stack.
p64::Frame g_frame;
p64::GifShowScene g_gif_show;
p64::Scene *const g_scenes[] = {&g_gif_show};
constexpr size_t kSceneCount = sizeof(g_scenes) / sizeof(g_scenes[0]);

// Frames presented per second for the on-panel counter, measured over ~0.5 s windows.
class FpsMeter {
 public:
  void reset(int64_t now_us) {
    window_start_us_ = now_us;
    frames_ = 0;
    fps_ = 0.0f;
  }
  void tick(int64_t now_us) {
    ++frames_;
    const int64_t elapsed = now_us - window_start_us_;
    if (elapsed >= 500000) {
      fps_ = static_cast<float>(frames_) * 1e6f / static_cast<float>(elapsed);
      frames_ = 0;
      window_start_us_ = now_us;
    }
  }
  float fps() const { return fps_; }

 private:
  int64_t window_start_us_ = 0;
  uint32_t frames_ = 0;
  float fps_ = 0.0f;
};

// Accumulates per-window statistics for the periodic log line.
struct StatsWindow {
  int64_t start_us = 0;
  uint32_t frames = 0;
  uint64_t render_us = 0;

  void reset(int64_t now_us) {
    start_us = now_us;
    frames = 0;
    render_us = 0;
  }

  void log(const char *name, int64_t now_us, p64::Display &display) const {
    const p64::Display::Stats s = display.take_stats();
    if (frames == 0) return;
    const float seconds = static_cast<float>(now_us - start_us) / 1e6f;
    const float n = static_cast<float>(frames);
    ESP_LOGI(TAG, "%s: %" PRIu32 " frames presented in %.1f s = %.1f fps; per frame render %.2f ms, wait %.2f ms, "
                  "copy %.2f ms; late flips %" PRIu32 ", sync timeouts %" PRIu32 " (%s)",
             name, frames, seconds, n / seconds, static_cast<float>(render_us) / n / 1000.0f,
             static_cast<float>(s.wait_us) / n / 1000.0f, static_cast<float>(s.copy_us) / n / 1000.0f,
             s.late_flips, s.timeouts, display.dma_sync() ? "frame-locked to the DMA" : "timed fallback");
  }
};

// Runs one scene until its duration elapses (never, for kRunForever) or BOOT is pressed.
void run_scene(p64::Scene &scene, p64::Display &display, p64::Button &boot) {
  scene.enter(display, g_frame);
  const int64_t t0 = esp_timer_get_time();
  int64_t t_prev = t0;
  FpsMeter meter;
  meter.reset(t0);
  StatsWindow window;
  window.reset(t0);
  const bool forever = scene.duration_ms() == p64::kRunForever;
  while (true) {
    const int64_t now = esp_timer_get_time();
    const uint32_t t_ms = static_cast<uint32_t>((now - t0) / 1000);
    if (!forever && t_ms >= scene.duration_ms()) break;
    if (boot.poll()) {
      ESP_LOGI(TAG, "BOOT pressed, restarting the scene");
      break;
    }
    const p64::FrameInfo info{t_ms, static_cast<float>(now - t_prev) / 1e6f, meter.fps()};
    t_prev = now;
    const bool dirty = scene.render(display, g_frame, info);
    window.render_us += static_cast<uint64_t>(esp_timer_get_time() - now);
    if (dirty) {
      display.wait_for_back_buffer();
      display.present(g_frame);
      meter.tick(esp_timer_get_time());
      ++window.frames;
    } else {
      vTaskDelay(pdMS_TO_TICKS(2));  // nothing new: poll the button and the schedule
    }
    if (now - window.start_us >= kStatsIntervalUs) {
      window.log(scene.name(), now, display);
      window.reset(now);
    }
  }
  window.log(scene.name(), esp_timer_get_time(), display);
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(TAG, "p64 firmware, built " __DATE__ " " __TIME__);
  ESP_LOGI(TAG, "brightness cap %u/255", static_cast<unsigned>(p64::max_brightness()));

  static p64::Display display;
  if (!display.begin()) {
    ESP_LOGE(TAG, "display failed to start; nothing to do");
    return;
  }
  static p64::Button boot;
  boot.begin();

  // Network, clock and the Makapix fetcher come up in the background (core 0).
  p64::clock::start(CONFIG_P64_TZ, CONFIG_P64_NTP_SERVER);
  if (p64::wifi::start(CONFIG_P64_WIFI_SSID, CONFIG_P64_WIFI_PASSWORD)) {
    p64::makapix::start();
    p64::speedtest::start();
  }

  for (size_t i = 0;; i = (i + 1) % kSceneCount) {
    p64::Scene &scene = *g_scenes[i];
    if (scene.duration_ms() == p64::kRunForever) {
      ESP_LOGI(TAG, "scene: %s, runs until BOOT is pressed; statistics every 10 s", scene.name());
    } else {
      ESP_LOGI(TAG, "scene: %s, %" PRIu32 " s (BOOT skips)", scene.name(), scene.duration_ms() / 1000);
    }
    run_scene(scene, display, boot);
  }
}

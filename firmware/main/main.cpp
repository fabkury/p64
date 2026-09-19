// p64 -- firmware entry point.
//
// Milestone M0: boot, start the display, show the boot animation, then a quiet idle
// pattern, with a statistics line every 10 s. The rendering runs in its own task on
// core 1 from the start (docs/architecture.md section 2); everything network- and
// storage-related lives on core 0 and arrives with later milestones.

#include <cinttypes>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "boot_animation.hpp"
#include "p64/display/display.hpp"
#include "p64/gfx/frame.hpp"

namespace {

constexpr const char *TAG = "p64";
constexpr uint32_t kBootAnimationMs = 2000;  // becomes a setting with the settings store (M3)
constexpr int64_t kStatsIntervalUs = 10 * 1000 * 1000;
constexpr uint8_t kBrightness = 255;  // the user brightness setting arrives with M3

// Off the task stacks: the frame is 12 KB.
p64::gfx::Frame g_frame;
p64::display::Display g_display;

void log_stats(const char *what, int64_t window_start_us, uint32_t frames, uint64_t render_us) {
  const p64::display::Display::Stats s = g_display.take_stats();
  if (frames == 0) return;
  const float seconds = static_cast<float>(esp_timer_get_time() - window_start_us) / 1e6f;
  const float n = static_cast<float>(frames);
  ESP_LOGI(TAG,
           "%s: %" PRIu32 " frames in %.1f s = %.1f fps; per frame render %.2f ms, wait %.2f ms, copy %.2f ms; "
           "late flips %" PRIu32 ", sync timeouts %" PRIu32 " (%s)",
           what, frames, seconds, n / seconds, static_cast<float>(render_us) / n / 1000.0f,
           static_cast<float>(s.wait_us) / n / 1000.0f, static_cast<float>(s.copy_us) / n / 1000.0f, s.late_flips,
           s.timeouts, g_display.dma_sync() ? "frame-locked to the DMA" : "timed fallback");
}

// Core 1: the only task that presents frames.
void render_task(void *) {
  const p64::BootAnimation boot;
  const p64::IdlePattern idle;
  const int64_t t0 = esp_timer_get_time();
  int64_t window_start = t0;
  uint32_t window_frames = 0;
  uint64_t window_render_us = 0;
  bool booting = true;
  while (true) {
    const int64_t now = esp_timer_get_time();
    const uint32_t t_ms = static_cast<uint32_t>((now - t0) / 1000);
    bool dirty;
    if (booting) {
      dirty = boot.render(g_frame, t_ms, kBootAnimationMs);
      if (!dirty) {
        booting = false;
        ESP_LOGI(TAG, "boot animation done after %" PRIu32 " ms; idle pattern until content exists", t_ms);
        idle.render(g_frame, t_ms);
        dirty = true;
      }
    } else {
      idle.render(g_frame, t_ms);
      dirty = true;
    }
    window_render_us += static_cast<uint64_t>(esp_timer_get_time() - now);
    if (dirty) {
      g_display.wait_for_back_buffer();
      g_display.present(g_frame);
      ++window_frames;
    }
    if (now - window_start >= kStatsIntervalUs) {
      log_stats(booting ? "boot" : "idle", window_start, window_frames, window_render_us);
      window_start = now;
      window_frames = 0;
      window_render_us = 0;
    }
  }
}

void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS needs erasing (%s)", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

}  // namespace

extern "C" void app_main() {
  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "p64 firmware %s (IDF %s), built %s %s", app->version, app->idf_ver, app->date, app->time);

  init_nvs();

  // This image booted far enough to run: cancel the bootloader's rollback. A later
  // milestone moves this behind a self-test (display up, settings readable).
  esp_ota_img_states_t state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGI(TAG, "first boot of this image: marking it valid");
    esp_ota_mark_app_valid_cancel_rollback();
  }

  if (!g_display.begin()) {
    ESP_LOGE(TAG, "display failed to start; nothing to do");
    return;
  }
  g_display.set_rotation(p64::gfx::Rotation::R90);  // the printed shell's orientation; a setting with M3
  g_display.set_brightness(kBrightness);

  // Render on core 1, above everything else there; 8 KB of stack for the drawing code.
  xTaskCreatePinnedToCore(render_task, "render", 8192, nullptr, 20, nullptr, 1);
  ESP_LOGI(TAG, "render task started on core 1; boot animation %" PRIu32 " ms", kBootAnimationMs);
}

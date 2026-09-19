#include "p64/playback/renderer.hpp"

#include <cinttypes>

#include "esp_log.h"
#include "esp_timer.h"
#include "p64/playback/player.hpp"

namespace p64::playback {
namespace {

constexpr const char *TAG = "render";
constexpr int64_t kStatsIntervalUs = 10 * 1000 * 1000;

}  // namespace

bool Renderer::start(display::Display &display, FrameQueue &queue, Player *player) {
  if (task_) return true;
  display_ = &display;
  queue_ = &queue;
  player_ = player;
  const BaseType_t ok = xTaskCreatePinnedToCore(&Renderer::task_entry, "render", 6144, this, 20, &task_, 1);
  return ok == pdPASS;
}

Renderer::Stats Renderer::take_stats() {
  std::lock_guard<std::mutex> lock(mutex_);
  const Stats out = stats_;
  stats_ = Stats{};
  return out;
}

void Renderer::task_entry(void *arg) { static_cast<Renderer *>(arg)->run(); }

void Renderer::log_window(int64_t window_start_us, uint32_t frames, uint32_t late, uint32_t skipped) {
  const display::Display::Stats d = display_->take_stats();
  Player::Stats p;
  if (player_) p = player_->take_stats();
  if (frames == 0 && p.frames == 0) return;
  const float seconds = static_cast<float>(esp_timer_get_time() - window_start_us) / 1e6f;
  const float n = frames > 0 ? static_cast<float>(frames) : 1.0f;
  const float pn = p.frames > 0 ? static_cast<float>(p.frames) : 1.0f;
  ESP_LOGI(TAG,
           "%" PRIu32 " frames in %.1f s = %.1f fps; decode %.2f ms, copy %.2f ms, wait %.2f ms per frame; late %" PRIu32
           " (decode late %" PRIu32 "), skipped %" PRIu32 ", late flips %" PRIu32 ", timeouts %" PRIu32 " (%s)",
           frames, seconds, static_cast<float>(frames) / seconds, static_cast<float>(p.decode_us) / pn / 1000.0f,
           static_cast<float>(d.copy_us) / n / 1000.0f, static_cast<float>(d.wait_us) / n / 1000.0f, late, p.late,
           skipped, d.late_flips, d.timeouts, display_->dma_sync() ? "frame-locked" : "timed fallback");
}

void Renderer::run() {
  int64_t window_start = esp_timer_get_time();
  uint32_t window_frames = 0, window_late = 0, window_skipped = 0;
  const int64_t period_us = static_cast<int64_t>(display_->refresh_period_us());
  while (true) {
    const int64_t now = esp_timer_get_time();
    if (now - window_start >= kStatsIntervalUs) {
      log_window(window_start, window_frames, window_late, window_skipped);
      window_start = now;
      window_frames = window_late = window_skipped = 0;
    }
    ReadySlot *slot = queue_->consumer_peek();
    if (!slot) {
      vTaskDelay(1);
      continue;
    }
    // A newer artwork generation is already queued behind this slot: cut to it.
    if (slot->generation != queue_->latest_generation()) {
      queue_->consumer_release();
      if (player_) player_->notify_slot_free();
      ++window_skipped;
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.skipped;
      continue;
    }
    // When the frame should become visible: its due time, but no earlier than the
    // previous frame's minimum stay (measured on the schedule, not on when the copy
    // finished, or every frame would slip by the copy time). A new generation cuts at once.
    // The schedule starts at the first frame's actual visibility, so it is attainable;
    // the player's due time only pulls a frame later than that when decoding was late.
    const bool scheduled = slot->generation == last_generation_ && last_visible_us_ != 0;
    const int64_t schedule_us = scheduled ? last_visible_us_ + last_delay_us_ : slot->due_us;
    const int64_t target = std::max<int64_t>(slot->due_us, schedule_us);
    // The copy into the driver takes several milliseconds (7.5 ms at 10 planes), so it
    // starts that much ahead of the target and the flip lands on the boundary after it.
    const int64_t start_at = target - copy_lead_us_;
    const int64_t wait = start_at - esp_timer_get_time();
    if (wait > 1000) vTaskDelay(pdMS_TO_TICKS(wait / 1000));
    display_->wait_for_back_buffer();
    const int64_t t_start = esp_timer_get_time();
    display_->present(slot->frame);
    const int64_t t_end = esp_timer_get_time();
    copy_lead_us_ = (copy_lead_us_ * 7 + (t_end - t_start)) / 8;
    // The frame is visible from the flip on (plus up to one refresh period); a present
    // that started late pushes the schedule of everything after it.
    const int64_t visible = std::max<int64_t>(target, t_end);
    const bool late = scheduled && t_end > schedule_us + period_us;
    last_present_us_ = t_end;
    last_visible_us_ = visible;
    last_delay_us_ = slot->delay_us;
    last_generation_ = slot->generation;
    queue_->consumer_release();
    if (player_) player_->notify_slot_free();
    ++window_frames;
    if (late) ++window_late;
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.frames;
    if (late) ++stats_.late;
  }
}

}  // namespace p64::playback

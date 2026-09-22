#include "p64/playback/renderer.hpp"

#include <cinttypes>
#include <new>

#include "esp_heap_caps.h"
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
  void *mem = heap_caps_malloc(sizeof(gfx::Frame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) mem = malloc(sizeof(gfx::Frame));
  preview_ = mem ? new (mem) gfx::Frame() : nullptr;
  // The render task keeps its stack in internal RAM: it is the real-time task.
  const BaseType_t ok = xTaskCreatePinnedToCore(&Renderer::task_entry, "render", 6144, this, 20, &task_, 1);
  return ok == pdPASS;
}

Renderer::Stats Renderer::take_stats() {
  std::lock_guard<std::mutex> lock(mutex_);
  const Stats out = stats_;
  totals_.frames += out.frames;
  totals_.late += out.late;
  totals_.skipped += out.skipped;
  stats_ = Stats{};
  return out;
}

Renderer::Stats Renderer::totals() {
  std::lock_guard<std::mutex> lock(mutex_);
  Stats t = totals_;
  t.frames += stats_.frames;
  t.late += stats_.late;
  t.skipped += stats_.skipped;
  return t;
}

void Renderer::request_mode(display::Mode mode) { pending_mode_.store(static_cast<int>(mode)); }

bool Renderer::snapshot(gfx::Frame &out) {
  std::lock_guard<std::mutex> lock(preview_mutex_);
  if (!have_preview_ || !preview_) return false;
  out.copy_from(*preview_);
  return true;
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
           "%" PRIu32 " frames in %.1f s = %.1f fps; decode %.2f ms (max %.1f), copy %.2f ms, wait %.2f ms per frame; "
           "late %" PRIu32 " (decode late %" PRIu32 "), skipped %" PRIu32 ", late flips %" PRIu32 ", timeouts %" PRIu32
           " (%s)",
           frames, seconds, static_cast<float>(frames) / seconds, static_cast<float>(p.decode_us) / pn / 1000.0f,
           static_cast<float>(p.max_decode_us) / 1000.0f, static_cast<float>(d.copy_us) / n / 1000.0f,
           static_cast<float>(d.wait_us) / n / 1000.0f, late, p.late, skipped, d.late_flips, d.timeouts,
           display_->dma_sync() ? "frame-locked" : "timed fallback");
}

void Renderer::run() {
  int64_t window_start = esp_timer_get_time();
  uint32_t window_frames = 0, window_late = 0, window_skipped = 0;
  int64_t period_us = static_cast<int64_t>(display_->refresh_period_us());
  while (true) {
    const int64_t now = esp_timer_get_time();
    if (now - window_start >= kStatsIntervalUs) {
      log_window(window_start, window_frames, window_late, window_skipped);
      window_start = now;
      window_frames = window_late = window_skipped = 0;
    }
    // A requested panel mode switch happens here, between frames: the driver's refresh
    // profile changes in place and the last picture is repainted (Display::set_mode).
    const int pending = pending_mode_.exchange(-1);
    if (pending >= 0 && static_cast<display::Mode>(pending) != display_->mode()) {
      display_->set_mode(static_cast<display::Mode>(pending));
      period_us = static_cast<int64_t>(display_->refresh_period_us());
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
    // Target and copy start from the schedule (timing.hpp): the due time, no earlier
    // than the previous frame's minimum stay; the copy (7.5 ms at 10 planes) starts that
    // much ahead so the flip lands on the boundary after the target.
    const timing::Schedule::Plan plan = schedule_.plan(slot->generation, slot->due_us);
    const int64_t start_at = plan.start_at_us;
    // Sleep towards the target in short steps: a newer generation announced meanwhile
    // (a swap, a widget) must not wait behind a frame due a minute from now.
    bool cut = false;
    while (true) {
      const int64_t remaining = start_at - esp_timer_get_time();
      if (remaining <= 1000) break;
      if (slot->generation != queue_->latest_generation()) {
        cut = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(std::min<int64_t>(remaining / 1000, 10)));
    }
    if (cut) continue;  // the loop's top drops the slot
    display_->wait_for_back_buffer();
    const int64_t t_start = esp_timer_get_time();
    display_->present(slot->frame);
    const int64_t t_end = esp_timer_get_time();
    const bool late =
        schedule_.presented(plan, slot->generation, slot->delay_us, slot->decoded_late, t_start, t_end, period_us);
    last_present_us_ = t_end;
    if (preview_) {
      std::lock_guard<std::mutex> lock(preview_mutex_);
      preview_->copy_from(slot->frame);
      have_preview_ = true;
    }
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

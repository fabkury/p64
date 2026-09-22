#include "p64/playback/player.hpp"
#include "p64/playback/timing.hpp"

#include <algorithm>

#include <new>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

namespace p64::playback {
namespace {

constexpr const char *TAG = "player";
constexpr uint32_t kMinFrameUs = timing::kMinFrameUs;
constexpr TickType_t kIdleWait = pdMS_TO_TICKS(100);

}  // namespace

bool Player::start(FrameQueue &queue) {
  if (task_) return true;
  queue_ = &queue;
  void *mem = heap_caps_malloc(sizeof(gfx::Frame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  last_frame_ = mem ? new (mem) gfx::Frame() : new gfx::Frame();
  commands_ = xQueueCreate(4, sizeof(std::shared_ptr<FrameSource> *));
  if (!commands_) return false;
  // Stack in PSRAM: the decoders keep their state on the heap and this task never
  // writes flash, so internal RAM stays for Wi-Fi and TLS.
  const BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(&Player::task_entry, "player", 12288, this, 15, &task_, 1,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ok == pdPASS;
}

void Player::play(std::shared_ptr<FrameSource> source) {
  if (!commands_) {
    ESP_LOGW(TAG, "play() before start(); ignored");
    return;
  }
  auto *boxed = new std::shared_ptr<FrameSource>(std::move(source));
  if (xQueueSend(commands_, &boxed, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "command queue full; dropping a play request");
    delete boxed;
    return;
  }
  if (task_) xTaskNotifyGive(task_);
}

std::shared_ptr<FrameSource> Player::current() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

void Player::notify_slot_free() {
  if (task_) xTaskNotifyGive(task_);
}

void Player::set_overlay(Overlay overlay) {
  std::lock_guard<std::mutex> lock(mutex_);
  overlay_ = std::move(overlay);
}

Player::Stats Player::take_stats() {
  std::lock_guard<std::mutex> lock(mutex_);
  const Stats out = stats_;
  stats_ = Stats{};
  return out;
}

bool Player::take_command(std::shared_ptr<FrameSource> &out, TickType_t wait) {
  std::shared_ptr<FrameSource> *boxed = nullptr;
  if (xQueueReceive(commands_, &boxed, wait) != pdTRUE) return false;
  out = std::move(*boxed);
  delete boxed;
  return true;
}

void Player::task_entry(void *arg) { static_cast<Player *>(arg)->run(); }

void Player::run() {
  std::shared_ptr<FrameSource> src;  // the task's own reference to the current source
  timing::Timeline timeline;          // due times (timing.hpp, host-tested)
  bool exhausted = false;  // static source fully produced: nothing more to ask for
  uint32_t last_overlay_key = 0;
  auto overlay_key = [this]() -> uint32_t {
    std::lock_guard<std::mutex> lock(mutex_);
    return overlay_.key ? overlay_.key() : 0;
  };
  auto overlay_draw = [this](gfx::Frame &f) {
    Overlay o;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      o = overlay_;
    }
    if (o.draw) o.draw(f);
  };
  while (true) {
    // A new command replaces the source at once (hard cut); when nothing plays, wait.
    std::shared_ptr<FrameSource> incoming;
    if (take_command(incoming, (!src || exhausted) ? kIdleWait : 0)) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = incoming;
        ++generation_;
      }
      queue_->announce_generation(generation_);  // the renderer frees the old slots now
      src = std::move(incoming);
      timeline.restart();
      exhausted = false;
      continue;
    }
    if (!src) continue;
    if (exhausted) {
      // A static frame stays; only a changed overlay makes it go out again.
      const uint32_t key = overlay_key();
      if (key == last_overlay_key) continue;
      ReadySlot *slot = queue_->producer_slot();
      if (!slot) continue;
      slot->frame.copy_from(*last_frame_);
      if (key) overlay_draw(slot->frame);
      last_overlay_key = key;
      slot->due_us = esp_timer_get_time();
      slot->delay_us = kMinFrameUs;
      slot->generation = generation_;
      slot->first = false;
      slot->decoded_late = false;
      queue_->producer_publish();
      continue;
    }

    ReadySlot *slot = queue_->producer_slot();
    if (!slot) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));  // the renderer frees a slot
      continue;
    }
    const int64_t t0 = esp_timer_get_time();
    uint32_t delay_ms = 0;
    const int64_t for_us = timeline.next_due_us();
    const bool ok = src->next_frame(slot->frame, delay_ms, for_us);
    const int64_t t1 = esp_timer_get_time();
    if (ok) {
      const uint32_t key = overlay_key();
      if (src->is_static()) last_frame_->copy_from(slot->frame);
      if (key) overlay_draw(slot->frame);
      last_overlay_key = key;
    }
    if (!ok) {
      ESP_LOGW(TAG, "%s: source failed: %s", src->name().c_str(), src->error());
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.errors;
      src.reset();
      current_.reset();
      continue;
    }
    // First frame: due at once; late: the timeline re-anchors here, nothing is skipped.
    const timing::Timeline::Stamp stamp = timeline.produced(t1, delay_ms);
    const bool late = stamp.late;
    const uint32_t delay_us = stamp.delay_us;
    slot->due_us = stamp.due_us;
    slot->delay_us = stamp.delay_us;
    slot->generation = generation_;
    slot->first = stamp.first;
    slot->decoded_late = stamp.late;
    queue_->producer_publish();
    if (late) {
      // Debug level: an artwork whose frames decode slower than their delays is late on
      // every frame by design (no-drop rule), and the render task's window line counts them.
      static int64_t last_report_us = 0;
      if (t1 - last_report_us > 1000000) {
        last_report_us = t1;
        ESP_LOGD(TAG, "late frame: %s frame %lu decoded %lld us after its due time (decode %lld us, delay %lu us)",
                 src->name().c_str(), static_cast<unsigned long>(stats_.frames), static_cast<long long>(t1 - for_us),
                 static_cast<long long>(t1 - t0), static_cast<unsigned long>(delay_us));
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.frames;
      stats_.decode_us += static_cast<uint64_t>(t1 - t0);
      stats_.max_decode_us = std::max<uint32_t>(stats_.max_decode_us, static_cast<uint32_t>(t1 - t0));
      if (late) ++stats_.late;
    }
    if (src->is_static()) exhausted = true;  // one frame is all there is
  }
}

}  // namespace p64::playback

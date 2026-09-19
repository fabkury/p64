#include "p64/playback/player.hpp"

#include <algorithm>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

namespace p64::playback {
namespace {

constexpr const char *TAG = "player";
constexpr uint32_t kMinFrameUs = 16667;  // the 60 fps presentation cap (spec 4.3)
constexpr TickType_t kIdleWait = pdMS_TO_TICKS(100);

}  // namespace

bool Player::start(FrameQueue &queue) {
  if (task_) return true;
  queue_ = &queue;
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
  int64_t next_due_us = 0;
  bool first = true;
  bool exhausted = false;  // static source fully produced: nothing more to ask for
  while (true) {
    // A new command replaces the source at once (hard cut); when nothing plays, wait.
    std::shared_ptr<FrameSource> incoming;
    if (take_command(incoming, (!src || exhausted) ? kIdleWait : 0)) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = incoming;
        ++generation_;
      }
      src = std::move(incoming);
      first = true;
      exhausted = false;
      continue;
    }
    if (!src || exhausted) continue;

    ReadySlot *slot = queue_->producer_slot();
    if (!slot) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));  // the renderer frees a slot
      continue;
    }
    const int64_t t0 = esp_timer_get_time();
    uint32_t delay_ms = 0;
    const bool ok = src->next_frame(slot->frame, delay_ms, first ? 0 : next_due_us);
    const int64_t t1 = esp_timer_get_time();
    if (!ok) {
      ESP_LOGW(TAG, "%s: source failed: %s", src->name().c_str(), src->error());
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.errors;
      src.reset();
      current_.reset();
      continue;
    }
    const uint32_t delay_us = std::max<uint32_t>(delay_ms * 1000u, kMinFrameUs);
    int64_t due;
    bool late = false;
    if (first) {
      due = t1;  // a new source shows as soon as its first frame exists
    } else if (t1 > next_due_us) {
      due = t1;  // produced late: the timeline re-anchors here, no frame is skipped
      late = true;
    } else {
      due = next_due_us;
    }
    slot->due_us = due;
    slot->delay_us = delay_us;
    slot->generation = generation_;
    slot->first = first;
    slot->decoded_late = late;
    queue_->producer_publish();
    if (late) {
      // Debug level: an artwork whose frames decode slower than their delays is late on
      // every frame by design (no-drop rule), and the render task's window line counts them.
      static int64_t last_report_us = 0;
      if (t1 - last_report_us > 1000000) {
        last_report_us = t1;
        ESP_LOGD(TAG, "late frame: %s frame %lu decoded %lld us after its due time (decode %lld us, delay %lu us)",
                 src->name().c_str(), static_cast<unsigned long>(stats_.frames), static_cast<long long>(t1 - next_due_us),
                 static_cast<long long>(t1 - t0), static_cast<unsigned long>(delay_us));
      }
    }
    next_due_us = due + delay_us;
    first = false;
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

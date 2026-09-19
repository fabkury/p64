#include "p64/playback/player.hpp"

#include <algorithm>

#include "esp_log.h"
#include "esp_timer.h"

namespace p64::playback {
namespace {

constexpr const char *TAG = "player";
constexpr uint32_t kMinFrameUs = 16667;  // the 60 fps presentation cap (spec 4.3)
constexpr TickType_t kIdleWait = pdMS_TO_TICKS(100);

}  // namespace

bool Player::start(FrameQueue &queue) {
  if (task_) return true;
  queue_ = &queue;
  commands_ = xQueueCreate(4, sizeof(std::shared_ptr<Artwork> *));
  if (!commands_) return false;
  const BaseType_t ok = xTaskCreatePinnedToCore(&Player::task_entry, "player", 12288, this, 15, &task_, 1);
  return ok == pdPASS;
}

void Player::play(std::shared_ptr<Artwork> artwork) {
  auto *boxed = new std::shared_ptr<Artwork>(std::move(artwork));
  if (xQueueSend(commands_, &boxed, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "command queue full; dropping a play request");
    delete boxed;
    return;
  }
  if (task_) xTaskNotifyGive(task_);
}

std::shared_ptr<Artwork> Player::current() const {
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

bool Player::take_command(std::shared_ptr<Artwork> &out, TickType_t wait) {
  std::shared_ptr<Artwork> *boxed = nullptr;
  if (xQueueReceive(commands_, &boxed, wait) != pdTRUE) return false;
  out = std::move(*boxed);
  delete boxed;
  return true;
}

void Player::task_entry(void *arg) { static_cast<Player *>(arg)->run(); }

void Player::run() {
  std::shared_ptr<Artwork> art;  // the task's own reference to the current artwork
  int64_t next_due_us = 0;
  bool first = true;
  bool exhausted = false;  // static image fully produced: nothing more to decode
  while (true) {
    // A new command replaces the artwork at once (hard cut); when nothing plays, wait.
    std::shared_ptr<Artwork> incoming;
    if (take_command(incoming, (!art || exhausted) ? kIdleWait : 0)) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = incoming;
        ++generation_;
      }
      art = std::move(incoming);
      first = true;
      exhausted = false;
      continue;
    }
    if (!art || exhausted) continue;

    ReadySlot *slot = queue_->producer_slot();
    if (!slot) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));  // the renderer frees a slot
      continue;
    }
    const int64_t t0 = esp_timer_get_time();
    uint32_t delay_ms = 0;
    const bool ok = art->next_frame(slot->frame, delay_ms);
    const int64_t t1 = esp_timer_get_time();
    if (!ok) {
      ESP_LOGW(TAG, "%s: decode failed after %lu frames: %s", art->name().c_str(),
               static_cast<unsigned long>(art->frames_decoded()), art->error());
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.errors;
      art.reset();
      current_.reset();
      continue;
    }
    const uint32_t delay_us = std::max<uint32_t>(delay_ms * 1000u, kMinFrameUs);
    int64_t due;
    bool late = false;
    if (first) {
      due = t1;  // a new artwork shows as soon as its first frame exists
    } else if (t1 > next_due_us) {
      due = t1;  // decoded late: the timeline re-anchors here, no frame is skipped
      late = true;
    } else {
      due = next_due_us;
    }
    slot->due_us = due;
    slot->delay_us = delay_us;
    slot->generation = generation_;
    slot->first = first;
    queue_->producer_publish();
    next_due_us = due + delay_us;
    first = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.frames;
      stats_.decode_us += static_cast<uint64_t>(t1 - t0);
      if (late) ++stats_.late;
    }
    if (art->is_static()) exhausted = true;  // one frame is all there is
  }
}

}  // namespace p64::playback

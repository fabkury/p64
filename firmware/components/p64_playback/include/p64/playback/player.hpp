// p64 -- Player: the core-1 task that decodes the current artwork ahead of time into the
// ready-frame queue, keeping the timeline (spec 4.3) and the no-drop rule (ADR 0003).
// Swapping artworks is a command from the main task; the new artwork's first frame is
// queued at once with a new generation, which the renderer treats as a hard cut.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "p64/playback/artwork.hpp"
#include "p64/playback/frame_queue.hpp"

namespace p64::playback {

class Player {
 public:
  struct Stats {
    uint32_t frames = 0;       // frames decoded and queued
    uint32_t late = 0;         // frames that were decoded after their due time (timeline re-anchored)
    uint64_t decode_us = 0;    // decode + scale time
    uint32_t errors = 0;       // decode failures (the artwork is dropped)
  };

  // Starts the task on core 1. `queue` must outlive the player.
  bool start(FrameQueue &queue);
  // Plays this artwork from its first frame, cutting whatever plays now. nullptr stops
  // production (the panel keeps its last frame).
  void play(std::shared_ptr<Artwork> artwork);
  std::shared_ptr<Artwork> current() const;
  uint32_t generation() const { return generation_; }
  // The renderer calls this after freeing a slot so a blocked player wakes at once.
  void notify_slot_free();
  Stats take_stats();

 private:
  static void task_entry(void *arg);
  void run();
  bool take_command(std::shared_ptr<Artwork> &out, TickType_t wait);

  FrameQueue *queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  QueueHandle_t commands_ = nullptr;  // holds raw pointers to heap-allocated shared_ptr copies
  mutable std::mutex mutex_;
  std::shared_ptr<Artwork> current_;
  uint32_t generation_ = 0;
  Stats stats_;
};

}  // namespace p64::playback

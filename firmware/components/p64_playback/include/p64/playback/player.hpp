// p64 -- Player: the core-1 task that produces the current source's frames ahead of
// time into the ready-frame queue, keeping the timeline (spec 4.3) and the no-drop rule
// (ADR 0003). Swapping sources is a command from the main task; the new source's first
// frame is queued at once with a new generation, which the renderer treats as a hard
// cut: the previous picture stays up until that frame exists (seamless, spec 3.6).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "p64/playback/frame_queue.hpp"
#include "p64/playback/frame_source.hpp"

namespace p64::playback {

class Player {
 public:
  struct Stats {
    uint32_t frames = 0;     // frames produced and queued
    uint32_t late = 0;       // frames produced after their due time (timeline re-anchored)
    uint64_t decode_us = 0;  // decode + scale time
    uint32_t max_decode_us = 0;  // the slowest frame in the window
    uint32_t errors = 0;     // source failures (the source is dropped)
  };

  // Starts the task on core 1. `queue` must outlive the player.
  bool start(FrameQueue &queue);
  // Plays this source from its first frame, cutting whatever plays now. nullptr stops
  // production (the panel keeps its last frame).
  void play(std::shared_ptr<FrameSource> source);
  std::shared_ptr<FrameSource> current() const;
  uint32_t generation() const { return generation_; }
  // The renderer calls this after freeing a slot so a blocked player wakes at once.
  void notify_slot_free();
  Stats take_stats();

  // An overlay drawn on every produced frame (the clock overlay, spec 6.1). `key()` is
  // asked before each frame: 0 means nothing to draw; a changed key makes a static source
  // re-emit its frame so the overlay moves on without a new decode.
  struct Overlay {
    std::function<uint32_t()> key;
    std::function<void(gfx::Frame &)> draw;
  };
  void set_overlay(Overlay overlay);

 private:
  static void task_entry(void *arg);
  void run();
  bool take_command(std::shared_ptr<FrameSource> &out, TickType_t wait);

  FrameQueue *queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  QueueHandle_t commands_ = nullptr;  // holds raw pointers to heap-allocated shared_ptr copies
  mutable std::mutex mutex_;
  std::shared_ptr<FrameSource> current_;
  uint32_t generation_ = 0;
  Stats stats_;
  Overlay overlay_;
  gfx::Frame *last_frame_ = nullptr;  // PSRAM: the last decoded frame without the overlay
};

}  // namespace p64::playback

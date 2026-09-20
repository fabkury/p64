// p64 -- Renderer: the core-1 task that presents ready frames on the panel, the only
// caller of Display::present(). It honours each slot's due time and minimum stay, cuts
// to a newer artwork generation immediately, counts late frames, and logs a statistics
// line every 10 s.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p64/display/display.hpp"
#include "p64/playback/frame_queue.hpp"

namespace p64::playback {

class Player;

class Renderer {
 public:
  struct Stats {
    uint32_t frames = 0;   // frames presented
    uint32_t late = 0;     // presented more than one panel period after their due time
    uint32_t skipped = 0;  // unpresented slots of an older generation dropped at a cut
  };

  // Starts the task on core 1. The player pointer may be null (nobody to wake).
  bool start(display::Display &display, FrameQueue &queue, Player *player);
  Stats take_stats();
  // Cumulative counters since start (for status).
  Stats totals();
  int64_t last_present_us() const { return last_present_us_; }
  // Asks for a panel mode switch; the render task switches the driver's refresh profile
  // between two frames (spec 3.1).
  void request_mode(display::Mode mode);
  // Copies the frame presented last (the live preview). False before the first frame.
  bool snapshot(gfx::Frame &out);

 private:
  static void task_entry(void *arg);
  void run();
  void log_window(int64_t window_start_us, uint32_t frames, uint32_t late, uint32_t skipped);

  display::Display *display_ = nullptr;
  FrameQueue *queue_ = nullptr;
  Player *player_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::mutex mutex_;
  Stats stats_;
  Stats totals_;
  std::atomic<int> pending_mode_{-1};  // -1 = none, else display::Mode
  std::mutex preview_mutex_;
  gfx::Frame *preview_ = nullptr;  // allocated in PSRAM by start()
  bool have_preview_ = false;
  int64_t last_present_us_ = 0;  // when the last copy finished (the flip was issued)
  int64_t last_visible_us_ = 0;  // when the last frame became visible on the schedule
  int64_t copy_lead_us_ = 7500;  // running average of the copy time; the copy starts this early
  uint32_t last_delay_us_ = 0;
  uint32_t last_generation_ = 0;
};

}  // namespace p64::playback

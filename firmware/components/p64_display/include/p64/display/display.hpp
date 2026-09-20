// p64 -- Display: the one owner of the HUB75 driver. Producers hand it logical Frames;
// it rotates them, applies the channel gains, copies them into the driver's back buffer
// and flips on a panel refresh boundary.
//
// Tear-free timing (ported from the hardware tests, see hardware-tests/README.md "Frame
// pacing"): the driver's flip only relinks the DMA descriptor chain and the DMA keeps
// scanning the old front buffer until that frame ends. The driver gives no signal, so
// Display watches the LCD GDMA channel: present() clears the channel's end-of-frame
// flag before the flip, and wait_for_back_buffer() sleeps until shortly before the next
// predicted boundary, spins on the flag, then checks from the descriptor addresses that
// the DMA really moved to the other chain. Rendering is thereby locked to the refresh.
//
// Panel modes are refresh profiles of the driver (p64 patch): Quality sends all ten bit
// planes at the sdkconfig minimum refresh rate (transition bit 4, 271 Hz on this panel
// at 20 MHz); Photo sends eight planes at 600 Hz minimum (transition bit 4, 814 Hz).
// A switch rebuilds the descriptor chains in place inside arrays allocated at begin()
// (nothing is allocated, so it cannot fail for lack of internal RAM) and repaints the
// last picture into both buffers with the new LUT; the panel shows the previous picture
// for a few refresh periods meanwhile.
#pragma once

#include <cstdint>
#include <mutex>

#include "p64/gfx/frame.hpp"
#include "p64/gfx/geometry.hpp"

class Hub75Driver;

namespace p64::display {

enum class Mode : uint8_t { Quality = 0, Photo = 1 };

class Display {
 public:
  struct Stats {
    uint32_t frames = 0;      // present() calls
    uint64_t copy_us = 0;     // rotation + copy into the driver's back buffer
    uint64_t wait_us = 0;     // time spent in wait_for_back_buffer()
    uint32_t late_flips = 0;  // flips that only took effect one refresh later
    uint32_t timeouts = 0;    // waits that gave up on the DMA flag and used the timed fallback
  };

  // Cumulative facts since begin(), for status and diagnostics.
  struct Health {
    bool stalled = false;  // the DMA stall detector fired (needs a reboot)
    uint32_t frames = 0;
    uint32_t late_flips = 0;
    uint32_t timeouts = 0;
    int dma_priority = -1;  // GDMA arbitration priority of the panel's channel
    int transition_bit = 0;  // bit planes 0..n sent once per frame
    int bit_depth = 0;       // bit planes sent per frame (10 in Quality, 8 in Photo)
    double refresh_hz = 0;
    Mode mode = Mode::Quality;
    int restarts = 0;      // driver re-creations (mode switches)
    bool dma_sync = false;  // frame boundaries come from the DMA (else timed waits)
    bool dma_moving = false;  // the DMA descriptor pointer advanced during the probe
  };

  // Builds the driver from sdkconfig (pins, panel, timing) in Quality mode and starts
  // the refresh. The panel shows black afterwards.
  bool begin();
  // Rotates and copies the frame into the back buffer and flips. Call
  // wait_for_back_buffer() first when less than one refresh period has passed since
  // the previous present().
  void present(const gfx::Frame &frame);
  // Blocks until the DMA has certainly finished with the back buffer.
  void wait_for_back_buffer();

  // 0 blanks the panel completely (pause, panel off), 1..255 is the panel brightness.
  // Takes effect on the next refresh.
  void set_brightness(uint8_t value);
  uint8_t brightness() const { return brightness_; }

  // Logical orientation applied at present(). Takes effect on the next present().
  void set_rotation(gfx::Rotation rotation) { rotation_ = rotation; }
  gfx::Rotation rotation() const { return rotation_; }

  // Per-channel gains in percent (50..100), applied at present().
  void set_gains(unsigned r_pct, unsigned g_pct, unsigned b_pct);

  // Switches the panel mode (the driver's refresh profile) in place and repaints the
  // last presented picture. Call from the render task between frames. Returns false
  // when the driver refused the profile; the previous mode then stays up.
  bool set_mode(Mode mode);
  Mode mode() const { return mode_; }

  // True when frame boundaries are read from the DMA (frame-locked mode).
  bool dma_sync() const { return lcd_dma_channel_ >= 0; }
  // Returns the counters accumulated since the previous call and resets them.
  Stats take_stats();
  Health health() const;
  // GDMA arbitration priority of the panel's channel, 0..5. Takes effect at once.
  bool set_dma_priority(int priority);
  // Panel refresh period in microseconds as the driver reports it.
  double refresh_period_us() const;

  // The one Display of the firmware (nullptr before begin()).
  static Display *instance() { return s_instance_; }

 private:
  bool start_driver(unsigned min_refresh_hz);
  void stop_driver();
  void push_physical();          // copies physical_ into the back buffer and flips
  void repaint_both_buffers();  // after a profile switch: both buffers get physical_
  bool learn_chains();
  bool wait_for_dma_switch();
  void wait_timed();
  void note_timeout(uint32_t fetching);  // stall detection (warns once)

  // Guards driver_ across the re-creation in set_mode(): the setters and health() may
  // be called from other tasks while the render task restarts the driver.
  std::mutex driver_mutex_;
  Hub75Driver *driver_ = nullptr;
  Mode mode_ = Mode::Quality;
  uint8_t brightness_ = 0;
  gfx::Rotation rotation_ = gfx::Rotation::R0;
  gfx::ChannelLut lut_{};
  int lcd_dma_channel_ = -1;
  double period_us_ = 0;      // refresh period from the driver (0 until begin())
  uint32_t chain_bytes_ = 0;  // one of the driver's descriptor chains, in bytes
  bool flip_pending_ = false;
  uint32_t chain_last_[2] = {0, 0};  // last descriptor of each of the driver's two chains
  uint32_t old_front_last_ = 0;      // last descriptor of the chain that was front at the flip
  uint32_t stall_dscr_ = 0;          // descriptor pointer seen at the previous timeout
  int stall_count_ = 0;              // consecutive timeouts with the pointer frozen
  bool stall_reported_ = false;
  int restarts_ = 0;
  int64_t last_flip_us_ = 0;
  int64_t last_boundary_us_ = 0;  // 0 until the first frame boundary has been observed
  int64_t last_yield_us_ = 0;     // when the wait last blocked (lets the idle task run)
  Stats stats_;
  Stats totals_;  // since begin()
  // Physical-orientation copy of the last presented frame (internal RAM, DMA-friendly).
  uint8_t physical_[gfx::kPanelWidth * gfx::kPanelHeight * 3] = {};
  static Display *s_instance_;
};

}  // namespace p64::display

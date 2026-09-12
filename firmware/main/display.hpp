// p64 -- display layer: an RGB888 frame in ordinary RAM plus a thin owner of the
// esp-hub75 driver. Scenes draw into a Frame; Display::present() pushes it to the
// panel in one call and flips the double buffer.
//
// Tear-free timing: the driver's flip only relinks the DMA descriptor chain, and the
// DMA keeps scanning the old front buffer until that frame ends, up to one refresh
// period later. The driver exposes no signal for that, so Display watches the LCD
// GDMA channel itself: present() clears the channel's end-of-frame flag before the
// flip, and wait_for_back_buffer() sleeps until shortly before the next expected
// frame boundary, spins on the flag, then checks from the descriptor addresses that
// the DMA really moved to the other buffer. Rendering can then be locked to the panel
// refresh (one frame per refresh, ~76 fps on this panel). If the channel cannot be
// found the wait falls back to a full refresh period after the flip.
#pragma once

#include <cstdint>

#include "sdkconfig.h"

class Hub75Driver;

namespace p64 {

constexpr int kWidth = CONFIG_HUB75_PANEL_WIDTH * CONFIG_HUB75_LAYOUT_COLS;
constexpr int kHeight = CONFIG_HUB75_PANEL_HEIGHT * CONFIG_HUB75_LAYOUT_ROWS;

struct Rgb {
  uint8_t r, g, b;
};

constexpr Rgb kBlack{0, 0, 0};
constexpr Rgb kWhite{255, 255, 255};

class Frame {
 public:
  static constexpr int width() { return kWidth; }
  static constexpr int height() { return kHeight; }

  void clear(Rgb c = kBlack);
  // Out-of-range coordinates are ignored, so callers can draw without clipping.
  void set(int x, int y, Rgb c);
  void fill_rect(int x, int y, int w, int h, Rgb c);
  // Disc in continuous coordinates: pixel (i, j) covers [i, i+1) x [j, j+1).
  void fill_disc(float cx, float cy, float radius, Rgb c);

  const uint8_t *data() const { return px_; }
  // Bulk write access: kWidth*kHeight*3 bytes, row-major RGB888.
  uint8_t *pixels() { return px_; }

 private:
  uint8_t px_[kWidth * kHeight * 3] = {};
};

class Display {
 public:
  struct Stats {
    uint32_t frames = 0;       // present() calls
    uint64_t copy_us = 0;      // time spent copying frames into the driver's back buffer
    uint64_t wait_us = 0;      // time spent in wait_for_back_buffer()
    uint32_t late_flips = 0;   // flips that only took effect one refresh later
    uint32_t timeouts = 0;     // waits that gave up on the DMA flag and used the timed fallback
  };

  // Builds the driver from sdkconfig (pins, panel, timing) and starts the refresh.
  bool begin();
  // Copies the frame into the back buffer and flips. Call wait_for_back_buffer() first
  // when less than one refresh period has passed since the previous present().
  void present(const Frame &frame);
  // Blocks until the DMA has certainly finished with the back buffer.
  void wait_for_back_buffer();
  // 0 blanks the panel completely, 255 is the driver maximum. Takes effect on the next refresh.
  void set_brightness(uint8_t value);
  uint8_t brightness() const { return brightness_; }
  // True when frame boundaries are read from the DMA (frame-locked mode).
  bool dma_sync() const { return lcd_dma_channel_ >= 0; }
  // Returns the counters accumulated since the previous call and resets them.
  Stats take_stats();

  // Panel refresh period implied by sdkconfig (standard two-scan wiring, full BCM).
  static constexpr double refresh_period_us();

 private:
  bool wait_for_dma_switch();
  void wait_timed();

  Hub75Driver *driver_ = nullptr;
  uint8_t brightness_ = 0;
  int lcd_dma_channel_ = -1;
  bool flip_pending_ = false;
  uint32_t old_front_last_ = 0;  // last descriptor of the chain that was front at the flip
  int64_t last_flip_us_ = 0;
  int64_t last_boundary_us_ = 0;  // 0 until the first frame boundary has been observed
  int64_t last_yield_us_ = 0;     // when the wait last blocked (lets the idle task run)
  Stats stats_;
};

// ---------------------------------------------------------------------------
// Refresh period, derived the same way the driver derives it:
//   rows (= panel_height / 2) x descriptors per row (2^(bits-1) for full BCM)
//   x DMA line width (all chained panels) / actual LCD clock (160 MHz / integer).
// 64x64, 8 bits, 20 MHz -> 32 x 128 x 64 / 20 MHz = 13107.2 us (76.3 Hz).
// If the driver has to shorten low bit planes to meet HUB75_MIN_REFRESH_RATE the real
// period is shorter than this, which only makes the timed fallback more conservative.
// ---------------------------------------------------------------------------

constexpr uint32_t kHub75RequestedClockHz =
#if defined(CONFIG_HUB75_CLK_8MHZ)
    8000000;
#elif defined(CONFIG_HUB75_CLK_10MHZ)
    10000000;
#elif defined(CONFIG_HUB75_CLK_16MHZ)
    16000000;
#elif defined(CONFIG_HUB75_CLK_18MHZ)
    18000000;
#elif defined(CONFIG_HUB75_CLK_23MHZ)
    23000000;
#elif defined(CONFIG_HUB75_CLK_27MHZ)
    27000000;
#elif defined(CONFIG_HUB75_CLK_32MHZ)
    32000000;
#else
    20000000;
#endif

constexpr uint32_t hub75_actual_clock_hz(uint32_t requested) {
  const uint32_t divider = (160000000u + requested / 2) / requested;
  return 160000000u / (divider < 2 ? 2 : divider);
}

constexpr uint32_t kHub75ScanRows = CONFIG_HUB75_PANEL_HEIGHT / 2;
constexpr uint32_t kHub75DescriptorsPerRow = 1u << (CONFIG_HUB75_BIT_DEPTH - 1);
constexpr uint32_t kHub75DmaWidth =
    static_cast<uint32_t>(CONFIG_HUB75_PANEL_WIDTH) * CONFIG_HUB75_LAYOUT_COLS * CONFIG_HUB75_LAYOUT_ROWS;

constexpr double Display::refresh_period_us() {
  return static_cast<double>(kHub75ScanRows) * kHub75DescriptorsPerRow * kHub75DmaWidth * 1e6 /
         hub75_actual_clock_hz(kHub75RequestedClockHz);
}

}  // namespace p64

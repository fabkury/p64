// p64 -- display layer: an RGB888 frame in ordinary RAM plus a thin owner of the
// esp-hub75 driver. Scenes draw into a Frame; Display::present() pushes it to the
// panel in one DMA-friendly call and flips the double buffer.
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

 private:
  uint8_t px_[kWidth * kHeight * 3] = {};
};

class Display {
 public:
  // Builds the driver from sdkconfig (pins, panel, timing) and starts the refresh.
  bool begin();
  void present(const Frame &frame);
  // 0 blanks the panel completely, 255 is the driver maximum. Takes effect on the next refresh.
  void set_brightness(uint8_t value);
  uint8_t brightness() const { return brightness_; }

 private:
  Hub75Driver *driver_ = nullptr;
  uint8_t brightness_ = 0;
};

}  // namespace p64

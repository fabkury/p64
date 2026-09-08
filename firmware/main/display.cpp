#include "display.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "hub75.h"

namespace p64 {
namespace {

constexpr const char *TAG = "display";

Hub75Config config_from_sdkconfig() {
  Hub75Config cfg{};
  cfg.panel_width = CONFIG_HUB75_PANEL_WIDTH;
  cfg.panel_height = CONFIG_HUB75_PANEL_HEIGHT;

#if defined(CONFIG_HUB75_WIRING_SCAN_1_4_16PX)
  cfg.scan_wiring = Hub75ScanWiring::SCAN_1_4_16PX_HIGH;
#elif defined(CONFIG_HUB75_WIRING_SCAN_1_8_32PX)
  cfg.scan_wiring = Hub75ScanWiring::SCAN_1_8_32PX_HIGH;
#elif defined(CONFIG_HUB75_WIRING_SCAN_1_8_32PX_FULL)
  cfg.scan_wiring = Hub75ScanWiring::SCAN_1_8_32PX_FULL;
#elif defined(CONFIG_HUB75_WIRING_SCAN_1_8_40PX)
  cfg.scan_wiring = Hub75ScanWiring::SCAN_1_8_40PX_HIGH;
#elif defined(CONFIG_HUB75_WIRING_SCAN_1_8_64PX)
  cfg.scan_wiring = Hub75ScanWiring::SCAN_1_8_64PX_HIGH;
#else
  cfg.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
#endif

#if defined(CONFIG_HUB75_DRIVER_FM6126A)
  cfg.shift_driver = Hub75ShiftDriver::FM6126A;
#elif defined(CONFIG_HUB75_DRIVER_FM6124)
  cfg.shift_driver = Hub75ShiftDriver::FM6124;
#elif defined(CONFIG_HUB75_DRIVER_MBI5124)
  cfg.shift_driver = Hub75ShiftDriver::MBI5124;
#elif defined(CONFIG_HUB75_DRIVER_DP3246)
  cfg.shift_driver = Hub75ShiftDriver::DP3246;
#else
  cfg.shift_driver = Hub75ShiftDriver::GENERIC;
#endif

  cfg.layout_rows = CONFIG_HUB75_LAYOUT_ROWS;
  cfg.layout_cols = CONFIG_HUB75_LAYOUT_COLS;
#if defined(CONFIG_HUB75_LAYOUT_TOP_LEFT_DOWN)
  cfg.layout = Hub75PanelLayout::TOP_LEFT_DOWN;
#elif defined(CONFIG_HUB75_LAYOUT_TOP_RIGHT_DOWN)
  cfg.layout = Hub75PanelLayout::TOP_RIGHT_DOWN;
#elif defined(CONFIG_HUB75_LAYOUT_BOTTOM_LEFT_UP)
  cfg.layout = Hub75PanelLayout::BOTTOM_LEFT_UP;
#elif defined(CONFIG_HUB75_LAYOUT_BOTTOM_RIGHT_UP)
  cfg.layout = Hub75PanelLayout::BOTTOM_RIGHT_UP;
#elif defined(CONFIG_HUB75_LAYOUT_TOP_LEFT_DOWN_ZIGZAG)
  cfg.layout = Hub75PanelLayout::TOP_LEFT_DOWN_ZIGZAG;
#elif defined(CONFIG_HUB75_LAYOUT_TOP_RIGHT_DOWN_ZIGZAG)
  cfg.layout = Hub75PanelLayout::TOP_RIGHT_DOWN_ZIGZAG;
#elif defined(CONFIG_HUB75_LAYOUT_BOTTOM_LEFT_UP_ZIGZAG)
  cfg.layout = Hub75PanelLayout::BOTTOM_LEFT_UP_ZIGZAG;
#elif defined(CONFIG_HUB75_LAYOUT_BOTTOM_RIGHT_UP_ZIGZAG)
  cfg.layout = Hub75PanelLayout::BOTTOM_RIGHT_UP_ZIGZAG;
#else
  cfg.layout = Hub75PanelLayout::HORIZONTAL;
#endif

#if defined(CONFIG_HUB75_ROTATE_90)
  cfg.rotation = Hub75Rotation::ROTATE_90;
#elif defined(CONFIG_HUB75_ROTATE_180)
  cfg.rotation = Hub75Rotation::ROTATE_180;
#elif defined(CONFIG_HUB75_ROTATE_270)
  cfg.rotation = Hub75Rotation::ROTATE_270;
#else
  cfg.rotation = Hub75Rotation::ROTATE_0;
#endif

  cfg.pins.r1 = CONFIG_HUB75_PIN_R1;
  cfg.pins.g1 = CONFIG_HUB75_PIN_G1;
  cfg.pins.b1 = CONFIG_HUB75_PIN_B1;
  cfg.pins.r2 = CONFIG_HUB75_PIN_R2;
  cfg.pins.g2 = CONFIG_HUB75_PIN_G2;
  cfg.pins.b2 = CONFIG_HUB75_PIN_B2;
  cfg.pins.a = CONFIG_HUB75_PIN_A;
  cfg.pins.b = CONFIG_HUB75_PIN_B;
  cfg.pins.c = CONFIG_HUB75_PIN_C;
  cfg.pins.d = CONFIG_HUB75_PIN_D;
  cfg.pins.e = CONFIG_HUB75_PIN_E;
  cfg.pins.lat = CONFIG_HUB75_PIN_LAT;
  cfg.pins.oe = CONFIG_HUB75_PIN_OE;
  cfg.pins.clk = CONFIG_HUB75_PIN_CLK;

#if defined(CONFIG_HUB75_CLK_8MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_8M;
#elif defined(CONFIG_HUB75_CLK_10MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_10M;
#elif defined(CONFIG_HUB75_CLK_16MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_16M;
#elif defined(CONFIG_HUB75_CLK_18MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_18M;
#elif defined(CONFIG_HUB75_CLK_23MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_23M;
#elif defined(CONFIG_HUB75_CLK_27MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_27M;
#elif defined(CONFIG_HUB75_CLK_32MHZ)
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_32M;
#else
  cfg.output_clock_speed = Hub75ClockSpeed::HZ_20M;
#endif
  cfg.min_refresh_rate = CONFIG_HUB75_MIN_REFRESH_RATE;
  cfg.latch_blanking = CONFIG_HUB75_LATCH_BLANKING;
  cfg.brightness = CONFIG_HUB75_BRIGHTNESS;

#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  cfg.double_buffer = true;
#endif
#if defined(CONFIG_HUB75_CLK_PHASE_INVERTED)
  cfg.clk_phase_inverted = true;
#endif
  return cfg;
}

const char *shift_driver_name(Hub75ShiftDriver d) {
  switch (d) {
    case Hub75ShiftDriver::GENERIC:
      return "GENERIC";
    case Hub75ShiftDriver::FM6126A:
    case Hub75ShiftDriver::ICN2038S:
      return "FM6126A/ICN2038S";
    case Hub75ShiftDriver::FM6124:
      return "FM6124";
    case Hub75ShiftDriver::MBI5124:
      return "MBI5124";
    case Hub75ShiftDriver::DP3246:
      return "DP3246";
  }
  return "?";
}

}  // namespace

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Frame::clear(Rgb c) {
  if (c.r == c.g && c.g == c.b) {
    std::memset(px_, c.r, sizeof(px_));
    return;
  }
  for (int i = 0; i < kWidth * kHeight; ++i) {
    px_[i * 3 + 0] = c.r;
    px_[i * 3 + 1] = c.g;
    px_[i * 3 + 2] = c.b;
  }
}

void Frame::set(int x, int y, Rgb c) {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
  uint8_t *p = &px_[(y * kWidth + x) * 3];
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
}

void Frame::fill_rect(int x, int y, int w, int h, Rgb c) {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, kWidth);
  const int y1 = std::min(y + h, kHeight);
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) set(xx, yy, c);
  }
}

void Frame::fill_disc(float cx, float cy, float radius, Rgb c) {
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
  const int x1 = std::min(kWidth - 1, static_cast<int>(std::ceil(cx + radius)));
  const int y1 = std::min(kHeight - 1, static_cast<int>(std::ceil(cy + radius)));
  const float r2 = radius * radius;
  for (int y = y0; y <= y1; ++y) {
    const float dy = static_cast<float>(y) + 0.5f - cy;
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx;
      if (dx * dx + dy * dy <= r2) set(x, y, c);
    }
  }
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

bool Display::begin() {
  if (driver_) return true;
  const Hub75Config cfg = config_from_sdkconfig();
  ESP_LOGI(TAG, "panel %ux%u, layout %ux%u, rotation %u, driver %s, clock %lu Hz, min refresh %u Hz, %s",
           cfg.panel_width, cfg.panel_height, cfg.layout_cols, cfg.layout_rows,
           static_cast<unsigned>(cfg.rotation), shift_driver_name(cfg.shift_driver),
           static_cast<unsigned long>(cfg.output_clock_speed), cfg.min_refresh_rate,
           cfg.double_buffer ? "double-buffered" : "single-buffered");
  ESP_LOGI(TAG, "pins R1=%d G1=%d B1=%d R2=%d G2=%d B2=%d A=%d B=%d C=%d D=%d E=%d LAT=%d OE=%d CLK=%d",
           cfg.pins.r1, cfg.pins.g1, cfg.pins.b1, cfg.pins.r2, cfg.pins.g2, cfg.pins.b2, cfg.pins.a,
           cfg.pins.b, cfg.pins.c, cfg.pins.d, cfg.pins.e, cfg.pins.lat, cfg.pins.oe, cfg.pins.clk);

  driver_ = new Hub75Driver(cfg);
  if (!driver_->begin()) {
    ESP_LOGE(TAG, "Hub75Driver::begin() failed");
    delete driver_;
    driver_ = nullptr;
    return false;
  }
  if (driver_->get_width() != kWidth || driver_->get_height() != kHeight) {
    ESP_LOGE(TAG, "driver reports %ux%u but Frame is %dx%d", driver_->get_width(), driver_->get_height(),
             kWidth, kHeight);
    driver_->end();
    delete driver_;
    driver_ = nullptr;
    return false;
  }
  brightness_ = cfg.brightness;
  driver_->clear();
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  driver_->flip_buffer();
  driver_->clear();
#endif
  ESP_LOGI(TAG, "HUB75 refresh running");
  return true;
}

void Display::present(const Frame &frame) {
  if (!driver_) return;
  driver_->draw_pixels(0, 0, kWidth, kHeight, frame.data(), Hub75PixelFormat::RGB888, Hub75ColorOrder::RGB,
                       false);
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  driver_->flip_buffer();
#endif
}

void Display::set_brightness(uint8_t value) {
  if (!driver_) return;
  brightness_ = value;
  driver_->set_brightness(value);
}

}  // namespace p64

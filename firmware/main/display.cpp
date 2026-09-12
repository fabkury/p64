#include "display.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/dma_types.h"
#include "hal/gdma_ll.h"
#include "hub75.h"
#include "soc/gdma_channel.h"
#include "soc/gdma_struct.h"
#include "soc/soc_caps.h"

namespace p64 {
namespace {

constexpr const char *TAG = "display";

// Timed fallback: extra time after the refresh period before the back buffer is
// touched, covering the descriptor the DMA may already have fetched plus jitter.
constexpr int64_t kFlipMarginUs = 700;
// DMA-synchronised wait: how long before the predicted boundary the task stops
// sleeping and starts spinning on the end-of-frame flag. Waking late is harmless
// (the flag is simply already set), so this only trades spin time for detection
// latency; the 1 ms tick adds up to 1 ms of early wake-up on top.
constexpr int64_t kSpinLeadUs = 600;
// Give up on the DMA flag after this many refresh periods and use the timed fallback.
constexpr int kSyncTimeoutPeriods = 3;
// Consecutive timeouts with the DMA's descriptor pointer frozen before a stall is
// reported.
constexpr int kStallTimeouts = 3;
// Longest the main task may go without blocking (keeps the idle task and its watchdog fed).
constexpr int64_t kForcedYieldUs = 1000 * 1000;

// One descriptor chain (one buffer) of the driver: descriptors per frame x their size.
constexpr uint32_t kChainBytes = kHub75ScanRows * kHub75DescriptorsPerRow * sizeof(dma_descriptor_t);

int find_lcd_dma_channel() {
  for (int ch = 0; ch < SOC_GDMA_PAIRS_PER_GROUP; ++ch) {
    if (GDMA.channel[ch].out.peri_sel.sel == SOC_GDMA_TRIG_PERIPH_LCD0) return ch;
  }
  return -1;
}

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

  cfg.output_clock_speed = static_cast<Hub75ClockSpeed>(kHub75RequestedClockHz);
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
  lcd_dma_channel_ = find_lcd_dma_channel();
#endif
  last_flip_us_ = esp_timer_get_time();
  flip_pending_ = false;
  ESP_LOGI(TAG, "HUB75 refresh running; expected refresh period %.1f us (%.1f Hz)", refresh_period_us(),
           1e6 / refresh_period_us());
  if (lcd_dma_channel_ >= 0) {
    ESP_LOGI(TAG, "frame boundaries read from GDMA channel %d (%lu-byte descriptor chains): frame-locked rendering",
             lcd_dma_channel_, static_cast<unsigned long>(kChainBytes));
  } else if (cfg.double_buffer) {
    ESP_LOGW(TAG, "LCD GDMA channel not found: frames wait a full refresh period after each flip");
  }
  return true;
}

void Display::present(const Frame &frame) {
  if (!driver_) return;
  const int64_t t0 = esp_timer_get_time();
  driver_->draw_pixels(0, 0, kWidth, kHeight, frame.data(), Hub75PixelFormat::RGB888, Hub75ColorOrder::RGB,
                       false);
  stats_.copy_us += static_cast<uint64_t>(esp_timer_get_time() - t0);
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  if (lcd_dma_channel_ >= 0) {
    const uint32_t ch = static_cast<uint32_t>(lcd_dma_channel_);
    // Before the flip the DMA loops in the front chain, so the last end-of-frame
    // descriptor address identifies that chain; wait_for_dma_switch() checks that
    // the DMA has left it. 0 means no frame has ended yet (right after begin()).
    old_front_last_ = GDMA.channel[ch].out.eof_des_addr;
    gdma_ll_tx_clear_interrupt_status(&GDMA, ch, GDMA_LL_EVENT_TX_EOF);
  }
  driver_->flip_buffer();
  flip_pending_ = true;
#endif
  last_flip_us_ = esp_timer_get_time();
  ++stats_.frames;
}

void Display::wait_for_back_buffer() {
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  if (!driver_ || !flip_pending_) return;
  const int64_t t0 = esp_timer_get_time();
  bool done = false;
  if (lcd_dma_channel_ >= 0) done = wait_for_dma_switch();
  if (!done) wait_timed();
  flip_pending_ = false;
  stats_.wait_us += static_cast<uint64_t>(esp_timer_get_time() - t0);
#endif
}

// Waits for the frame boundary at which the DMA actually moved to the other chain.
// Returns false on timeout (caller falls back to the timed wait).
bool Display::wait_for_dma_switch() {
  const uint32_t ch = static_cast<uint32_t>(lcd_dma_channel_);
  const double period = refresh_period_us();
  if (old_front_last_ == 0) return false;  // no chain identity yet: use the timed wait
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(kSyncTimeoutPeriods * period);
  while (true) {
    // Sleep until shortly before the next predicted boundary, then spin on the flag.
    // The main task must block now and then or the idle task (which feeds the task
    // watchdog) never runs on this core: sleep whenever a whole tick of slack exists,
    // and force a one-tick yield at least once a second even when it does not.
    const int64_t now = esp_timer_get_time();
    int64_t remaining = 0;
    if (last_boundary_us_ != 0) {
      const double elapsed = static_cast<double>(now - last_boundary_us_);
      const double periods_ahead = std::ceil(elapsed / period);
      const int64_t predicted = last_boundary_us_ + static_cast<int64_t>(periods_ahead * period);
      remaining = predicted - now - kSpinLeadUs;
    }
    if (remaining >= 1000) {
      vTaskDelay(pdMS_TO_TICKS(remaining / 1000));
      last_yield_us_ = now;
    } else if (now - last_yield_us_ > kForcedYieldUs) {
      vTaskDelay(1);
      last_yield_us_ = now;
    }
    while (!(gdma_ll_tx_get_interrupt_status(&GDMA, ch, true) & GDMA_LL_EVENT_TX_EOF)) {
      if (esp_timer_get_time() > deadline) {
        ++stats_.timeouts;
        note_timeout(GDMA.channel[ch].out.dscr);
        return false;
      }
    }
    stall_count_ = 0;
    last_boundary_us_ = esp_timer_get_time();
    gdma_ll_tx_clear_interrupt_status(&GDMA, ch, GDMA_LL_EVENT_TX_EOF);
    // A frame boundary has passed. If the descriptor being fetched now still lies in
    // the chain that was front when we flipped, the DMA looped instead of switching
    // (the relink came too late for that boundary): wait for the next one. Once it
    // is anywhere else, the old front buffer is free, however many boundaries ago
    // the switch happened.
    const uint32_t fetching = GDMA.channel[ch].out.dscr;
    const bool still_old = (old_front_last_ - fetching) < kChainBytes;  // unsigned: wraps when fetching > last
    if (!still_old) return true;
    ++stats_.late_flips;
  }
}

void Display::wait_timed() {
  const int64_t target = last_flip_us_ + static_cast<int64_t>(refresh_period_us()) + kFlipMarginUs;
  const int64_t remaining = target - esp_timer_get_time();
  if (remaining > 2000) vTaskDelay(pdMS_TO_TICKS((remaining - 1000) / 1000));
  while (esp_timer_get_time() < target) {
  }
}

// A refresh without an end-of-frame is not proof of a stall (the flag may simply have
// been missed), but the same descriptor pointer on several consecutive timeouts is:
// a running DMA advances it every few microseconds. The panel's DMA stops for good
// when its LCD FIFO underruns, which heavy traffic from another GDMA user can cause
// at high pixel clocks (seen with the hardware AES/SHA engines during TLS at 32 MHz;
// hence software crypto in sdkconfig.defaults). Stopping and restarting the driver in
// place was tried and does not bring the DMA back, so this only reports.
void Display::note_timeout(uint32_t fetching) {
  if (fetching == stall_dscr_) {
    ++stall_count_;
  } else {
    stall_dscr_ = fetching;
    stall_count_ = 1;
  }
  if (stall_count_ >= kStallTimeouts && !stall_reported_) {
    stall_reported_ = true;
    ESP_LOGE(TAG,
             "panel DMA stalled (descriptor pointer frozen at 0x%lx): another DMA user is starving the LCD FIFO; "
             "lower the HUB75 clock or remove that DMA traffic, then reboot",
             static_cast<unsigned long>(stall_dscr_));
  }
}

void Display::set_brightness(uint8_t value) {
  if (!driver_) return;
  brightness_ = value;
  driver_->set_brightness(value);
}

Display::Stats Display::take_stats() {
  const Stats out = stats_;
  stats_ = Stats{};
  return out;
}

}  // namespace p64

#include "p64/display/display.hpp"

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
#include "sdkconfig.h"
#include "soc/gdma_channel.h"
#include "soc/gdma_struct.h"
#include "soc/soc_caps.h"

namespace p64::display {

Display *Display::s_instance_ = nullptr;

namespace {

constexpr const char *TAG = "display";

// Minimum refresh rate the driver is asked for in each mode. Quality is the sdkconfig
// value (250 -> transition bit 4, 271 Hz); Photo selects transition bit 6 (698 Hz).
constexpr unsigned kQualityMinRefreshHz = CONFIG_HUB75_MIN_REFRESH_RATE;
constexpr unsigned kPhotoMinRefreshHz = 600;

// Timed fallback: extra time after the refresh period before the back buffer is
// touched, covering the descriptor the DMA may already have fetched plus jitter.
constexpr int64_t kFlipMarginUs = 700;
// DMA-synchronised wait: how long before the predicted boundary the task stops sleeping
// and starts spinning on the end-of-frame flag.
constexpr int64_t kSpinLeadUs = 600;
// Give up on the DMA flag after this many refresh periods and use the timed fallback.
constexpr int kSyncTimeoutPeriods = 3;
// Consecutive timeouts with the DMA's descriptor pointer frozen before a stall is reported.
constexpr int kStallTimeouts = 3;
// Longest the calling task may go without blocking (keeps the idle task and its watchdog fed).
constexpr int64_t kForcedYieldUs = 1000 * 1000;

constexpr uint32_t kRequestedClockHz =
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

// Fallback chain size with full binary code modulation; begin() asks the driver.
constexpr uint32_t kNominalChainBytes =
    (CONFIG_HUB75_PANEL_HEIGHT / 2) * (1u << (CONFIG_HUB75_BIT_DEPTH - 1)) * sizeof(dma_descriptor_t);

// Whether a descriptor address lies in the chain of `chain_bytes` whose last descriptor is `last`.
inline bool in_chain(uint32_t addr, uint32_t last, uint32_t chain_bytes) {
  return (last - addr) < chain_bytes;  // unsigned: wraps when addr > last
}

int find_lcd_dma_channel() {
  for (int ch = 0; ch < SOC_GDMA_PAIRS_PER_GROUP; ++ch) {
    if (GDMA.channel[ch].out.peri_sel.sel == SOC_GDMA_TRIG_PERIPH_LCD0) return ch;
  }
  return -1;
}

Hub75Config config_from_sdkconfig(unsigned min_refresh_hz) {
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
  cfg.layout = Hub75PanelLayout::HORIZONTAL;
  cfg.rotation = Hub75Rotation::ROTATE_0;  // p64 rotates in its own layer

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

  cfg.output_clock_speed = static_cast<Hub75ClockSpeed>(kRequestedClockHz);
  cfg.min_refresh_rate = static_cast<uint16_t>(min_refresh_hz);
  cfg.latch_blanking = CONFIG_HUB75_LATCH_BLANKING;
  cfg.brightness = 0;  // black until the first present(); the app sets the brightness
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  cfg.double_buffer = true;
#endif
#if defined(CONFIG_HUB75_CLK_PHASE_INVERTED)
  cfg.clk_phase_inverted = true;
#endif
  return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------

bool Display::begin() {
  if (driver_) return true;
  lut_.set_gains(100, 100, 100);
  ESP_LOGI(TAG, "panel %dx%d, pins R1=%d G1=%d B1=%d R2=%d G2=%d B2=%d A=%d B=%d C=%d D=%d E=%d LAT=%d OE=%d CLK=%d",
           gfx::kPanelWidth, gfx::kPanelHeight, CONFIG_HUB75_PIN_R1, CONFIG_HUB75_PIN_G1, CONFIG_HUB75_PIN_B1,
           CONFIG_HUB75_PIN_R2, CONFIG_HUB75_PIN_G2, CONFIG_HUB75_PIN_B2, CONFIG_HUB75_PIN_A, CONFIG_HUB75_PIN_B,
           CONFIG_HUB75_PIN_C, CONFIG_HUB75_PIN_D, CONFIG_HUB75_PIN_E, CONFIG_HUB75_PIN_LAT, CONFIG_HUB75_PIN_OE,
           CONFIG_HUB75_PIN_CLK);
  mode_ = Mode::Quality;
  if (!start_driver(kQualityMinRefreshHz)) return false;
  s_instance_ = this;
  return true;
}

bool Display::start_driver(unsigned min_refresh_hz) {
  const Hub75Config cfg = config_from_sdkconfig(min_refresh_hz);
  driver_ = new Hub75Driver(cfg);
  if (!driver_->begin()) {
    ESP_LOGE(TAG, "Hub75Driver::begin() failed");
    delete driver_;
    driver_ = nullptr;
    return false;
  }
  if (driver_->get_width() != gfx::kPanelWidth || driver_->get_height() != gfx::kPanelHeight) {
    ESP_LOGE(TAG, "driver reports %ux%u but the frame is %dx%d", driver_->get_width(), driver_->get_height(),
             gfx::kPanelWidth, gfx::kPanelHeight);
    stop_driver();
    return false;
  }
  period_us_ = driver_->get_frame_period_us();
  chain_bytes_ = static_cast<uint32_t>(driver_->get_descriptor_count() * sizeof(dma_descriptor_t));
  if (chain_bytes_ == 0) chain_bytes_ = kNominalChainBytes;
  driver_->clear();
  lcd_dma_channel_ = -1;
  last_boundary_us_ = 0;
  stall_dscr_ = 0;
  stall_count_ = 0;
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  lcd_dma_channel_ = find_lcd_dma_channel();
  if (lcd_dma_channel_ >= 0 && !learn_chains()) lcd_dma_channel_ = -1;
  if (lcd_dma_channel_ < 0) driver_->flip_buffer();
  driver_->clear();
#endif
  driver_->set_brightness(brightness_);
  last_flip_us_ = esp_timer_get_time();
  flip_pending_ = false;
  const int transition = driver_->get_lsb_msb_transition_bit();
  ESP_LOGI(TAG,
           "HUB75 refresh running (%s mode): %d bit planes, planes 0..%d sent once with halving output-enable "
           "windows, %u transmissions per frame; refresh period %.1f us (%.1f Hz), GDMA priority %d",
           mode_ == Mode::Photo ? "photo" : "quality", CONFIG_HUB75_BIT_DEPTH, transition,
           static_cast<unsigned>(driver_->get_descriptor_count()), refresh_period_us(), 1e6 / refresh_period_us(),
           driver_->get_dma_priority());
  if (lcd_dma_channel_ >= 0) {
    ESP_LOGI(TAG, "frame boundaries read from GDMA channel %d (%lu-byte descriptor chains): frame-locked rendering",
             lcd_dma_channel_, static_cast<unsigned long>(chain_bytes_));
  } else if (cfg.double_buffer) {
    ESP_LOGW(TAG, "LCD GDMA channel not found: frames wait a full refresh period after each flip");
  }
  return true;
}

void Display::stop_driver() {
  if (!driver_) return;
  driver_->end();
  delete driver_;
  driver_ = nullptr;
  lcd_dma_channel_ = -1;
  flip_pending_ = false;
}

bool Display::set_mode(Mode mode) {
  if (!driver_) return false;
  if (mode == mode_) return true;
  const Mode previous = mode_;
  const int64_t t0 = esp_timer_get_time();
  stop_driver();
  mode_ = mode;
  ++restarts_;
  if (!start_driver(mode == Mode::Photo ? kPhotoMinRefreshHz : kQualityMinRefreshHz)) {
    ESP_LOGE(TAG, "mode switch failed; restoring the previous mode");
    mode_ = previous;
    if (!start_driver(previous == Mode::Photo ? kPhotoMinRefreshHz : kQualityMinRefreshHz)) return false;
    return false;
  }
  // Put the last picture back so the blank is as short as the restart itself.
  driver_->draw_pixels(0, 0, gfx::kPanelWidth, gfx::kPanelHeight, physical_, Hub75PixelFormat::RGB888,
                       Hub75ColorOrder::RGB, false);
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  driver_->flip_buffer();
  flip_pending_ = true;
#endif
  last_flip_us_ = esp_timer_get_time();
  ESP_LOGI(TAG, "panel mode switched in %lld ms", static_cast<long long>((esp_timer_get_time() - t0) / 1000));
  return true;
}

void Display::present(const gfx::Frame &frame) {
  if (!driver_) return;
  const int64_t t0 = esp_timer_get_time();
  gfx::rotate_copy(frame, physical_, rotation_, lut_);
  driver_->draw_pixels(0, 0, gfx::kPanelWidth, gfx::kPanelHeight, physical_, Hub75PixelFormat::RGB888,
                       Hub75ColorOrder::RGB, false);
  stats_.copy_us += static_cast<uint64_t>(esp_timer_get_time() - t0);
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
  if (lcd_dma_channel_ >= 0) {
    const uint32_t ch = static_cast<uint32_t>(lcd_dma_channel_);
    // Before the flip the DMA loops in the front chain; the descriptor it is fetching
    // says which of the two that is, and wait_for_dma_switch() then checks that the
    // DMA has left it. (eof_des_addr lags one frame and named the wrong chain whenever
    // two presents came within a frame.)
    const uint32_t fetching = GDMA.channel[ch].out.dscr;
    old_front_last_ = in_chain(fetching, chain_last_[1], chain_bytes_) ? chain_last_[1] : chain_last_[0];
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
  if (old_front_last_ == 0) return false;
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(kSyncTimeoutPeriods * period);
  while (true) {
    // Sleep until shortly before the next predicted boundary, then spin on the flag.
    // The task must block now and then or the idle task (which feeds the watchdog)
    // never runs on this core: sleep whenever a whole tick of slack exists, and force
    // a one-tick yield at least once a second even when it does not.
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
    bool saw_edge = false;  // the boundary time is only accurate when the flag was seen to set
    while (!(gdma_ll_tx_get_interrupt_status(&GDMA, ch, true) & GDMA_LL_EVENT_TX_EOF)) {
      saw_edge = true;
      if (esp_timer_get_time() > deadline) {
        ++stats_.timeouts;
        note_timeout(GDMA.channel[ch].out.dscr);
        return false;
      }
    }
    stall_count_ = 0;
    if (saw_edge) last_boundary_us_ = esp_timer_get_time();
    gdma_ll_tx_clear_interrupt_status(&GDMA, ch, GDMA_LL_EVENT_TX_EOF);
    // A frame boundary has passed. If the descriptor being fetched now still lies in
    // the chain that was front when we flipped, the DMA looped instead of switching:
    // wait for the next boundary. Once it is anywhere else, the old front buffer is free.
    const uint32_t fetching = GDMA.channel[ch].out.dscr;
    const bool still_old = in_chain(fetching, old_front_last_, chain_bytes_);
    if (!still_old) return true;
    ++stats_.late_flips;
    if (esp_timer_get_time() > deadline) {
      ++stats_.timeouts;
      note_timeout(fetching);
      return false;
    }
  }
}

// Learns the last descriptor of both chains: the DMA loops chain 0 after begin(); after
// a flip it loops chain 1. eof_des_addr names the chain whose frame ended last, so it is
// read a few refresh periods after each change. Leaves chain 1 as the front buffer.
bool Display::learn_chains() {
  const uint32_t ch = static_cast<uint32_t>(lcd_dma_channel_);
  const TickType_t settle = pdMS_TO_TICKS(static_cast<uint32_t>(3 * refresh_period_us() / 1000) + 2);
  vTaskDelay(settle);
  chain_last_[0] = GDMA.channel[ch].out.eof_des_addr;
  driver_->flip_buffer();
  vTaskDelay(settle);
  chain_last_[1] = GDMA.channel[ch].out.eof_des_addr;
  if (chain_last_[0] == 0 || chain_last_[1] == 0 || chain_last_[0] == chain_last_[1]) {
    ESP_LOGW(TAG, "could not identify both descriptor chains (0x%lx, 0x%lx): timed waits instead",
             static_cast<unsigned long>(chain_last_[0]), static_cast<unsigned long>(chain_last_[1]));
    return false;
  }
  return true;
}

void Display::wait_timed() {
  const int64_t target = last_flip_us_ + static_cast<int64_t>(refresh_period_us()) + kFlipMarginUs;
  const int64_t remaining = target - esp_timer_get_time();
  if (remaining > 2000) vTaskDelay(pdMS_TO_TICKS((remaining - 1000) / 1000));
  while (esp_timer_get_time() < target) {
  }
}

// A refresh without an end-of-frame is not proof of a stall, but the same descriptor
// pointer on several consecutive timeouts is: a running DMA advances it every few
// microseconds. The panel's DMA stops for good when its LCD FIFO underruns (another
// GDMA user winning arbitration for too long); restarting the driver in place did not
// bring it back in the hardware tests, so this only reports.
void Display::note_timeout(uint32_t fetching) {
  if (fetching == stall_dscr_) {
    ++stall_count_;
  } else {
    stall_dscr_ = fetching;
    stall_count_ = 1;
  }
  if (stall_count_ >= kStallTimeouts && !stall_reported_) {
    stall_reported_ = true;
    ESP_LOGE(TAG, "panel DMA stalled (descriptor pointer frozen at 0x%lx): another DMA user is starving the LCD FIFO",
             static_cast<unsigned long>(stall_dscr_));
  }
}

Display::Health Display::health() const {
  Health h;
  h.stalled = stall_reported_;
  h.frames = totals_.frames + stats_.frames;
  h.late_flips = totals_.late_flips + stats_.late_flips;
  h.timeouts = totals_.timeouts + stats_.timeouts;
  h.dma_priority = driver_ ? driver_->get_dma_priority() : -1;
  h.transition_bit = driver_ ? driver_->get_lsb_msb_transition_bit() : 0;
  h.bit_depth = CONFIG_HUB75_BIT_DEPTH;
  h.refresh_hz = period_us_ > 0 ? 1e6 / period_us_ : 0;
  h.mode = mode_;
  h.restarts = restarts_;
  return h;
}

double Display::refresh_period_us() const {
  if (period_us_ > 0) return period_us_;
  return static_cast<double>(kNominalChainBytes / sizeof(dma_descriptor_t)) * gfx::kPanelWidth * 1e6 /
         kRequestedClockHz;
}

bool Display::set_dma_priority(int priority) { return driver_ && driver_->set_dma_priority(priority); }

void Display::set_brightness(uint8_t value) {
  brightness_ = value;
  if (driver_) driver_->set_brightness(value);
}

void Display::set_gains(unsigned r_pct, unsigned g_pct, unsigned b_pct) {
  lut_.set_gains(std::clamp(r_pct, 50u, 100u), std::clamp(g_pct, 50u, 100u), std::clamp(b_pct, 50u, 100u));
}

Display::Stats Display::take_stats() {
  const Stats out = stats_;
  totals_.frames += out.frames;
  totals_.copy_us += out.copy_us;
  totals_.wait_us += out.wait_us;
  totals_.late_flips += out.late_flips;
  totals_.timeouts += out.timeouts;
  stats_ = Stats{};
  return out;
}

}  // namespace p64::display

#include "button.hpp"

#include "driver/gpio.h"
#include "esp_timer.h"

namespace p64 {
namespace {

constexpr gpio_num_t kPin = GPIO_NUM_0;
constexpr int64_t kDebounceUs = 30 * 1000;

bool pressed_now() { return gpio_get_level(kPin) == 0; }

}  // namespace

void Button::begin() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << kPin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  raw_ = stable_ = pressed_now();
  raw_since_us_ = esp_timer_get_time();
}

bool Button::poll() {
  const bool raw = pressed_now();
  const int64_t now = esp_timer_get_time();
  if (raw != raw_) {
    raw_ = raw;
    raw_since_us_ = now;
  }
  if (raw_ != stable_ && now - raw_since_us_ >= kDebounceUs) {
    stable_ = raw_;
    return stable_;  // true only on the press edge
  }
  return false;
}

}  // namespace p64

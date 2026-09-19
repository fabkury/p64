#include "tap.hpp"

#include <cmath>

namespace p64::inputs {

float TapDetector::threshold_for(int sensitivity) {
  if (sensitivity < 1) sensitivity = 1;
  if (sensitivity > 10) sensitivity = 10;
  // 1 -> 2.5 g, 5 -> 1.5 g, 10 -> 0.25 g.
  return 2.75f - 0.25f * static_cast<float>(sensitivity);
}

void TapDetector::set_sensitivity(int sensitivity) { threshold_ = threshold_for(sensitivity); }

void TapDetector::reset() {
  lp_ready_ = false;
  above_ = false;
  pending_ = false;
  lockout_until_ = 0;
  peak_ = 0;
}

Tap TapDetector::feed(uint32_t t_ms, float ax, float ay, float az) {
  const float mag = std::sqrt(ax * ax + ay * ay + az * az);
  if (!lp_ready_) {
    lp_ = mag;
    lp_ready_ = true;
    last_ms_ = t_ms;
    return Tap::None;
  }
  // Gravity tracks slowly (about 150 ms); a tap is what is left above it.
  const uint32_t dt = t_ms - last_ms_;
  last_ms_ = t_ms;
  const float alpha = dt >= 150 ? 1.0f : static_cast<float>(dt) / 150.0f;
  const float hp = std::fabs(mag - lp_);
  lp_ += alpha * (mag - lp_);
  if (hp > peak_) peak_ = hp;

  Tap result = Tap::None;
  const bool locked = static_cast<int32_t>(t_ms - lockout_until_) < 0;
  if (!above_) {
    if (hp >= threshold_ && !locked) {
      above_ = true;
      above_since_ = t_ms;
    }
  } else if (hp < threshold_ * 0.5f) {
    above_ = false;
    const uint32_t length = t_ms - above_since_;
    if (length <= kImpulseMaxMs) {
      if (pending_) {
        const uint32_t gap = above_since_ - first_tap_ms_;
        if (gap >= kDoubleMinMs && gap <= kDoubleWindowMs) {
          pending_ = false;
          lockout_until_ = t_ms + kLockoutMs;
          result = Tap::Double;
        }
        // Too soon after the first: ringing of the same knock; ignore.
      } else {
        pending_ = true;
        first_tap_ms_ = above_since_;
      }
    } else {
      pending_ = false;  // a long excursion cancels a waiting tap: the shell was moved
    }
  } else if (t_ms - above_since_ > kImpulseMaxMs) {
    // Still high after the impulse window: motion; give up on it.
    above_ = false;
    pending_ = false;
    lockout_until_ = t_ms + kLockoutMs / 2;
  }
  if (pending_ && !above_ && t_ms - first_tap_ms_ > kDoubleWindowMs) {
    pending_ = false;
    lockout_until_ = t_ms + kLockoutMs;
    result = Tap::Single;
  }
  return result;
}

float TapDetector::take_peak() {
  const float p = peak_;
  peak_ = 0;
  return p;
}

}  // namespace p64::inputs

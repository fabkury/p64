#include "orientation.hpp"

#include <cmath>

namespace p64::inputs {
namespace {

constexpr float kPi = 3.14159265358979f;

float wrap180(float deg) {
  while (deg > 180.0f) deg -= 360.0f;
  while (deg <= -180.0f) deg += 360.0f;
  return deg;
}

}  // namespace

void OrientationTracker::calibrate(float angle_deg, uint16_t rotation, int sign) {
  cal_angle_ = angle_deg;
  cal_rotation_ = rotation % 360;
  sign_ = sign < 0 ? -1 : 1;
  calibrated_ = true;
  resolved_ = false;
  candidate_valid_ = false;
}

uint16_t OrientationTracker::candidate_for(float angle) const {
  const float rel = wrap180(angle - cal_angle_);
  const int steps = static_cast<int>(std::lround(rel / 90.0f));
  int r = static_cast<int>(cal_rotation_) + sign_ * steps * 90;
  r %= 360;
  if (r < 0) r += 360;
  return static_cast<uint16_t>(r);
}

bool OrientationTracker::feed(uint32_t t_ms, float ax, float ay, float az) {
  if (!lp_ready_) {
    gx_ = ax;
    gy_ = ay;
    gz_ = az;
    lp_ready_ = true;
    last_ms_ = t_ms;
    return false;
  }
  const uint32_t dt = t_ms - last_ms_;
  last_ms_ = t_ms;
  const float alpha = dt >= 500 ? 1.0f : static_cast<float>(dt) / 500.0f;  // about half a second
  gx_ += alpha * (ax - gx_);
  gy_ += alpha * (ay - gy_);
  gz_ += alpha * (az - gz_);
  in_plane_ = std::sqrt(gx_ * gx_ + gy_ * gy_);
  angle_ = std::atan2(gy_, gx_) * 180.0f / kPi;
  if (!calibrated_) return false;
  if (in_plane_ < kMinInPlaneG) {
    candidate_valid_ = false;
    return false;
  }
  const float rel = wrap180(angle_ - cal_angle_);
  const float off = std::fabs(wrap180(rel - 90.0f * std::lround(rel / 90.0f)));
  if (off > kMaxOffAxisDeg) {
    candidate_valid_ = false;
    return false;
  }
  const uint16_t c = candidate_for(angle_);
  if (!candidate_valid_ || c != candidate_) {
    candidate_ = c;
    candidate_valid_ = true;
    candidate_since_ = t_ms;
    if (!resolved_) {
      // The first reading resolves at once (boot); later changes wait out the hold.
      resolved_ = true;
      rotation_ = c;
      return true;
    }
    return false;
  }
  if (c != rotation_ && t_ms - candidate_since_ >= kHoldMs) {
    rotation_ = c;
    return true;
  }
  return false;
}

}  // namespace p64::inputs

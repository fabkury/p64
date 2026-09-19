// p64 -- auto-rotation from gravity (spec 3.4), pure and host-tested. The IMU sits on
// the driver board behind the panel, so gravity's direction in the board plane says
// which panel edge is down. A calibration ("the panel is upright now") stores the angle
// that means the current rotation setting; the tracker then resolves the multiple of
// 90 degrees nearest to the low-passed angle, with hysteresis: a new value must hold
// for a second, within 30 degrees of a right angle, with enough gravity in the plane
// (a panel lying flat keeps the last value).
#pragma once

#include <cstdint>

namespace p64::inputs {

class OrientationTracker {
 public:
  // Reference: with the panel upright at `rotation` (0/90/180/270), gravity pointed at
  // `angle_deg` in the board plane. `sign` is +1 or -1 (which way the panel's rotation
  // grows with the angle; a board-mounting fact).
  void calibrate(float angle_deg, uint16_t rotation, int sign);
  bool calibrated() const { return calibrated_; }
  // One sample at `t_ms`; returns true when the resolved rotation changed.
  bool feed(uint32_t t_ms, float ax, float ay, float az);
  uint16_t rotation() const { return rotation_; }
  bool resolved() const { return resolved_; }
  // Diagnostics.
  float angle_deg() const { return angle_; }
  float in_plane_g() const { return in_plane_; }
  float gx() const { return gx_; }
  float gy() const { return gy_; }
  float gz() const { return gz_; }

  static constexpr float kMinInPlaneG = 0.55f;   // below this the panel lies flat: hold
  static constexpr float kMaxOffAxisDeg = 30.0f;  // a tilted desk does not flip
  static constexpr uint32_t kHoldMs = 1000;

 private:
  uint16_t candidate_for(float angle) const;

  float gx_ = 0, gy_ = 0, gz_ = 0;
  bool lp_ready_ = false;
  uint32_t last_ms_ = 0;
  float angle_ = 0, in_plane_ = 0;
  float cal_angle_ = 0;
  uint16_t cal_rotation_ = 0;
  int sign_ = 1;
  bool calibrated_ = false;
  bool resolved_ = false;
  uint16_t rotation_ = 0;
  uint16_t candidate_ = 0;
  bool candidate_valid_ = false;
  uint32_t candidate_since_ = 0;
};

}  // namespace p64::inputs

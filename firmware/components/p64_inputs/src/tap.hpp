// p64 -- tap detection on accelerometer samples (spec 9), pure and host-tested. A tap is
// a short impulse: the high-passed acceleration magnitude crosses a threshold and falls
// back within a few tens of milliseconds (a longer excursion is the shell being moved).
// Two impulses within the double window make a double tap; a lone impulse is reported
// once the window closes; a 1 s lockout follows every report.
#pragma once

#include <cstdint>

namespace p64::inputs {

enum class Tap : uint8_t { None, Single, Double };

class TapDetector {
 public:
  // Sensitivity 1 (firm knock) .. 10 (light touch) sets the threshold in g.
  void set_sensitivity(int sensitivity);
  static float threshold_for(int sensitivity);
  // One sample; `t_ms` monotonic. Returns the gesture decided at this sample, if any.
  Tap feed(uint32_t t_ms, float ax, float ay, float az);
  // The largest high-passed magnitude seen since the last call (diagnostics).
  float take_peak();
  void reset();

  static constexpr uint32_t kImpulseMaxMs = 60;    // an excursion longer than this is motion, not a tap
  static constexpr uint32_t kDoubleMinMs = 80;     // second impulse no sooner than this after the first
  static constexpr uint32_t kDoubleWindowMs = 350; // and no later than this
  static constexpr uint32_t kLockoutMs = 1000;

 private:
  float threshold_ = 1.5f;
  float lp_ = 0;            // low-passed magnitude (gravity)
  bool lp_ready_ = false;
  bool above_ = false;      // inside an excursion
  uint32_t above_since_ = 0;
  uint32_t first_tap_ms_ = 0;  // a first impulse waiting for a second
  bool pending_ = false;
  uint32_t lockout_until_ = 0;
  uint32_t last_ms_ = 0;
  float peak_ = 0;
};

}  // namespace p64::inputs

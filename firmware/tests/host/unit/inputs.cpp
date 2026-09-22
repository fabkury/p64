// Host unit tests: taps and auto-rotation.
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


// --- taps and auto-rotation (spec 9, 3.4) ------------------------------------------

// Feeds a resting panel (gravity 1 g on z) with impulses of `g` for `len_ms` at the
// given times, 4 ms apart, and collects the gestures.
struct TapRun {
  p64::inputs::TapDetector det;
  uint32_t t = 0;
  int singles = 0, doubles = 0;
  void rest(uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 4) note(det.feed(t, 0.0f, 0.0f, 1.0f));
  }
  void impulse(float g, uint32_t len_ms) {
    for (uint32_t end = t + len_ms; t < end; t += 4) note(det.feed(t, 0.0f, 0.0f, 1.0f + g));
  }
  void note(p64::inputs::Tap tap) {
    if (tap == p64::inputs::Tap::Single) ++singles;
    if (tap == p64::inputs::Tap::Double) ++doubles;
  }
};

TEST_CASE("taps") {
  using p64::inputs::TapDetector;
  CHECK((TapDetector::threshold_for(1) > 2.4f && TapDetector::threshold_for(1) < 2.6f));
  CHECK((TapDetector::threshold_for(10) > 0.2f && TapDetector::threshold_for(10) < 0.3f));
  CHECK(TapDetector::threshold_for(5) > TapDetector::threshold_for(6));
  TapRun r;
  r.det.set_sensitivity(5);  // 1.5 g
  r.rest(1000);
  r.impulse(2.5f, 12);       // a knock: 12 ms
  r.rest(600);               // the double window closes: single
  CHECK_EQ(r.singles, 1); CHECK_EQ(r.doubles, 0);
  r.rest(1000);
  r.impulse(2.5f, 12);
  r.rest(150);
  r.impulse(2.5f, 12);       // a second knock 150 ms later: double
  r.rest(600);
  CHECK_EQ(r.singles, 1); CHECK_EQ(r.doubles, 1);
  r.rest(1000);
  r.impulse(0.8f, 12);       // below the threshold: nothing
  r.rest(600);
  CHECK_EQ(r.singles, 1); CHECK_EQ(r.doubles, 1);
  r.impulse(2.5f, 200);      // a long excursion: the shell moved, not a tap
  r.rest(800);
  CHECK_EQ(r.singles, 1); CHECK_EQ(r.doubles, 1);
  r.rest(1000);
  r.impulse(2.5f, 12);       // lockout: a knock right after a gesture is ignored
  r.rest(600);
  CHECK_EQ(r.singles, 2);
  r.impulse(2.5f, 12);
  r.rest(600);
  CHECK_EQ(r.singles, 2);    // inside the 1 s lockout
  r.rest(1000);
  r.impulse(2.5f, 12);
  r.rest(30);
  r.impulse(2.5f, 12);       // 30 ms later: ringing of the same knock, not a double
  r.rest(600);
  CHECK_EQ(r.singles, 3); CHECK_EQ(r.doubles, 1);
  CHECK(r.det.take_peak() > 2.0f);
  CHECK(r.det.take_peak() < 0.1f);
}


TEST_CASE("orientation") {
  using p64::inputs::OrientationTracker;
  OrientationTracker o;
  uint32_t t = 0;
  auto feed = [&](float ax, float ay, float az, uint32_t ms) {
    bool changed = false;
    for (uint32_t end = t + ms; t < end; t += 4) changed = o.feed(t, ax, ay, az) || changed;
    return changed;
  };
  feed(0.0f, -1.0f, 0.0f, 1500);        // gravity along -y: the panel stands, angle -90
  CHECK(!o.resolved());                 // not calibrated: nothing resolves
  CHECK((o.angle_deg() < -85.0f && o.angle_deg() > -95.0f));
  CHECK(o.in_plane_g() > 0.9f);
  o.calibrate(o.angle_deg(), 90, 1);    // this is "upright at rotation 90"
  CHECK(feed(0.0f, -1.0f, 0.0f, 100));  // resolves at once
  CHECK_EQ(o.rotation(), 90);
  // Turn 90 degrees: gravity moves to +x (angle 0): rotation 90 + 90 = 180 after the hold.
  bool changed = feed(1.0f, 0.0f, 0.0f, 400);
  CHECK(!changed);                      // the low-pass and the hold: not yet
  changed = feed(1.0f, 0.0f, 0.0f, 1600);
  CHECK(changed);
  CHECK_EQ(o.rotation(), 180);
  // A tilted desk (20 degrees off) keeps the value.
  changed = feed(0.94f, 0.34f, 0.0f, 3000);
  CHECK(!changed);
  CHECK_EQ(o.rotation(), 180);
  // Lying flat (gravity on z): holds the last value.
  changed = feed(0.0f, 0.0f, 1.0f, 3000);
  CHECK(!changed);
  CHECK_EQ(o.rotation(), 180);
  // Back to the calibrated pose: 90 again.
  changed = feed(0.0f, -1.0f, 0.0f, 3000);
  CHECK(changed);
  CHECK_EQ(o.rotation(), 90);
  // The other direction, and the sign flips it.
  OrientationTracker o2;
  t = 0;
  auto feed2 = [&](float ax, float ay, float az, uint32_t ms) {
    bool c = false;
    for (uint32_t end = t + ms; t < end; t += 4) c = o2.feed(t, ax, ay, az) || c;
    return c;
  };
  feed2(0.0f, -1.0f, 0.0f, 1500);
  o2.calibrate(o2.angle_deg(), 90, -1);
  feed2(0.0f, -1.0f, 0.0f, 100);
  CHECK_EQ(o2.rotation(), 90);
  feed2(1.0f, 0.0f, 0.0f, 2500);
  CHECK_EQ(o2.rotation(), 0);           // 90 - 90
  feed2(0.0f, 1.0f, 0.0f, 2500);        // 180 degrees from the reference: 90 - 180 = 270
  CHECK_EQ(o2.rotation(), 270);
}

}  // namespace

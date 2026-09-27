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

// --- the p64b rotary encoders (docs/hardware/encoders-soldered.md, section 5) ---------

TEST_CASE("seesaw wire format") {
  using namespace p64::inputs::seesaw;
  // Register addresses are two bytes, module then register (Adafruit_seesaw.h).
  const auto a = reg_address(kEncoder, kEncoderPosition);
  CHECK_EQ(a[0], 0x11); CHECK_EQ(a[1], 0x30);
  CHECK_EQ(kStatus, 0x00); CHECK_EQ(kStatusHwId, 0x01); CHECK_EQ(kGpio, 0x01); CHECK_EQ(kGpioBulk, 0x04);
  CHECK_EQ(kNeoPixel, 0x0E); CHECK_EQ(kEncoderDelta, 0x40);
  // 32-bit values travel big-endian; the position is signed.
  const auto be = be32(0x01020304u);
  CHECK_EQ(be[0], 1); CHECK_EQ(be[3], 4);
  const uint8_t minus_two[4] = {0xFF, 0xFF, 0xFF, 0xFE};
  CHECK_EQ(from_be32_signed(minus_two), -2);
  const uint8_t seven[4] = {0, 0, 0, 7};
  CHECK_EQ(from_be32_signed(seven), 7);
  // The switch is seesaw pin 24 with a pull-up: bit 24 of the bulk read, 0 = pressed.
  CHECK_EQ(pin_mask(kSwitchPin), 0x01000000u);
  CHECK(switch_pressed(0x00000000u));
  CHECK(!switch_pressed(0x01000000u));
  CHECK(switch_pressed(0xFEFFFFFFu));
  const auto pull = gpio_mask_command(kGpioPullEnSet, pin_mask(kSwitchPin));
  CHECK_EQ(pull[0], 0x01); CHECK_EQ(pull[1], 0x0B); CHECK_EQ(pull[2], 0x01); CHECK_EQ(pull[5], 0x00);
  // The reset writes 0xFF to STATUS/SWRST.
  const auto rst = reset_command();
  CHECK_EQ(rst[0], 0x00); CHECK_EQ(rst[1], 0x7F); CHECK_EQ(rst[2], 0xFF);
  // The NeoPixel: pin 6, one pixel of three bytes, GRB order after a 16-bit offset.
  CHECK_EQ(neopixel_pin_command()[2], 6);
  const auto len = neopixel_length_command();
  CHECK_EQ(len[2], 0); CHECK_EQ(len[3], 3);
  const auto colour = neopixel_colour_command(10, 20, 30);
  CHECK_EQ(colour[1], kNeoPixelBuf); CHECK_EQ(colour[2], 0); CHECK_EQ(colour[3], 0);
  CHECK_EQ(colour[4], 20); CHECK_EQ(colour[5], 10); CHECK_EQ(colour[6], 30);
  CHECK(known_hw_id(0x87));
  CHECK(!known_hw_id(0x00));
}

TEST_CASE("encoder tracker") {
  using p64::inputs::EncoderTracker;
  EncoderTracker t;
  // The first poll only records the position.
  auto ev = t.feed(0, 100, false);
  CHECK(!ev.any());
  CHECK_EQ(t.position(), 100);
  // Detents are position differences; several between polls arrive together.
  ev = t.feed(20, 101, false);
  CHECK_EQ(ev.turned, 1);
  ev = t.feed(40, 104, false);
  CHECK_EQ(ev.turned, 3);
  ev = t.feed(60, 102, false);
  CHECK_EQ(ev.turned, -2);
  CHECK_EQ(t.detents(), 2);
  // Wrap-safe across the 32-bit boundary.
  EncoderTracker w;
  w.feed(0, INT32_MAX, false);
  CHECK_EQ(w.feed(20, INT32_MIN, false).turned, 1);
  CHECK_EQ(w.feed(40, INT32_MAX, false).turned, -1);
  // Invert flips the sign of the turn and of the running count.
  EncoderTracker inv;
  inv.set_invert(true);
  inv.feed(0, 0, false);
  CHECK_EQ(inv.feed(20, 5, false).turned, -5);
  CHECK_EQ(inv.detents(), -5);
  // A resync after a board reset does not count the jump back to zero.
  t.resync();
  ev = t.feed(80, 0, false);
  CHECK_EQ(ev.turned, 0);
  CHECK_EQ(t.detents(), 2);
  // The switch: one sample of a level is bounce, two make a press; the release the same.
  EncoderTracker s;
  s.feed(0, 0, false);
  ev = s.feed(20, 0, true);
  CHECK(!ev.pressed);
  CHECK(!s.held());
  ev = s.feed(40, 0, true);
  CHECK(ev.pressed);
  CHECK(s.held());
  CHECK_EQ(s.presses(), 1);
  ev = s.feed(60, 0, false);   // a bounce while held
  CHECK(!ev.released);
  ev = s.feed(80, 0, true);
  CHECK(!ev.pressed);          // still the same press
  ev = s.feed(100, 0, false);
  ev = s.feed(120, 0, false);
  CHECK(ev.released);
  CHECK(!s.held());
  CHECK_EQ(s.presses(), 1);
  CHECK_EQ(s.long_presses(), 0);
  // A hold of kLongPressMs reports a long press once, before the release.
  uint32_t ms = 1000;
  s.feed(ms, 0, true);
  ev = s.feed(ms += 20, 0, true);
  CHECK(ev.pressed);
  int longs = 0;
  for (int i = 0; i < 60; ++i) {  // 1.2 s held
    ev = s.feed(ms += 20, 0, true);
    if (ev.long_press) ++longs;
  }
  CHECK_EQ(longs, 1);
  CHECK_EQ(s.long_presses(), 1);
  ev = s.feed(ms += 20, 0, false);
  ev = s.feed(ms += 20, 0, false);
  CHECK(ev.released);
  CHECK(!ev.long_press);
  // A short press never becomes a long one.
  s.feed(ms += 20, 0, true);
  ev = s.feed(ms += 20, 0, true);
  CHECK(ev.pressed);
  s.feed(ms += 200, 0, false);
  ev = s.feed(ms += 20, 0, false);
  CHECK(ev.released);
  CHECK_EQ(s.long_presses(), 1);
}

TEST_CASE("knob rules: roles and brightness steps") {
  using p64::inputs::brightness_after;
  using p64::inputs::KnobRole;
  using p64::inputs::role_of;
  // Knob A (0) is brightness, B (1) navigates; swap exchanges them.
  CHECK(role_of(0, false) == KnobRole::Brightness);
  CHECK(role_of(1, false) == KnobRole::Navigate);
  CHECK(role_of(0, true) == KnobRole::Navigate);
  CHECK(role_of(1, true) == KnobRole::Brightness);
  // A fixed step of kDetentStep (7) per detent, even in perceived lightness (spec 3.2,
  // 2026-09-27), clamped to 1..255.
  CHECK_EQ(p64::inputs::kDetentStep, 7);
  CHECK_EQ(brightness_after(255, 1), 255);
  CHECK_EQ(brightness_after(255, -1), 248);
  CHECK_EQ(brightness_after(100, 1), 107);
  CHECK_EQ(brightness_after(100, -1), 93);
  CHECK_EQ(brightness_after(1, 1), 8);
  CHECK_EQ(brightness_after(1, -1), 1);
  CHECK_EQ(brightness_after(5, -3), 1);
  CHECK_EQ(brightness_after(0, 1), 8);        // 0 is not a brightness: treated as 1
  CHECK_EQ(brightness_after(253, 1), 255);    // the last step is shorter
  CHECK_EQ(brightness_after(3, -1), 1);
  // Monotone, every detent visible, and the whole range is 37 detents each way (about
  // a turn and a half of a 24-detent knob).
  int ups = 0;
  for (uint8_t v = 1; v < 255; ++ups) {
    const uint8_t n = brightness_after(v, 1);
    CHECK(n > v);
    v = n;
  }
  CHECK_EQ(ups, 37);
  int downs = 0;
  for (uint8_t v = 255; v > 1; ++downs) {
    const uint8_t n = brightness_after(v, -1);
    CHECK(n < v);
    v = n;
  }
  CHECK_EQ(downs, 37);
  // N detents at once equal N single detents, and a spin down and back returns.
  CHECK_EQ(brightness_after(40, 5), brightness_after(brightness_after(40, 2), 3));
  CHECK_EQ(brightness_after(brightness_after(100, -5), 5), 100);
  CHECK_EQ(brightness_after(1, 1000), 255);
  CHECK_EQ(brightness_after(255, -1000), 1);
}

}  // namespace

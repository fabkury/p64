// Host unit tests: the themed clock faces (spec 7.1): the solar model against the almanac,
// the words grid, the flip's animation state, the hold times, the orrery's arithmetic, the
// hourglass's sand. The pixels themselves are compared with the mock's references by
// tests/host/run.py (tests/host/corpus/clock/).
#include "common.hpp"
#include "faces.hpp"
#include "solar.hpp"
#include "themed.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;
using p64::system::ClockFace;
namespace faces = p64::widgets::faces;
namespace themed = p64::widgets::themed;
namespace solar = p64::widgets::solar;

// Saturday 2026-09-26 at a local time.
tm moment(int h, int m, int s = 0) {
  tm t{};
  t.tm_year = 126;
  t.tm_mon = 8;
  t.tm_mday = 26;
  t.tm_wday = 6;
  t.tm_yday = 268;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;
  return t;
}

int lit_count(const Frame &f, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const Rgb c = f.get(x, y);
      if (c.r + c.g + c.b > 60) ++n;
    }
  return n;
}

bool same(const Frame &a, const Frame &b) { return std::memcmp(a.data(), b.data(), Frame::bytes()) == 0; }

// New York on 2026-09-26: 40.7 N, 74.0 W, UTC-4.
constexpr float kNyLat = 40.7f, kNyLon = -74.0f, kNyTz = -4;

TEST_CASE("solar: New York's sun on 2026-09-26 rises about 06:50 and sets about 18:45") {
  // the almanac says 06:50 and 18:50 for the refracted upper limb; the model's geometric
  // centre crosses the horizon a few minutes inside those
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 6 + 40 / 60.0f).elevation < 0);
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 7.0f).elevation > 0);
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 18 + 35 / 60.0f).elevation > 0);
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 18 + 55 / 60.0f).elevation < 0);
  // near the equinox the sun culminates at 90 - latitude, in the south
  const solar::Position noon = solar::sun(kNyLat, kNyLon, kNyTz, 268, 12 + 53 / 60.0f);
  CHECK(noon.elevation == doctest::Approx(90 - kNyLat).epsilon(0.03));
  CHECK(noon.azimuth == doctest::Approx(180).epsilon(0.02));
  // it rises in the east and sets in the west
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 7).azimuth == doctest::Approx(90).epsilon(0.05));
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 18.5f).azimuth == doctest::Approx(270).epsilon(0.05));
  // midnight: well below the horizon
  CHECK(solar::sun(kNyLat, kNyLon, kNyTz, 268, 0.5f).elevation < -40);
}

TEST_CASE("solar: New York's moon against the almanac (full on 2026-09-26, new on 2026-10-10)") {
  // full at 12:49 EDT on 2026-09-26, rising at 18:35 in the east (the almanac's refracted
  // upper limb; the model's centre crosses a few minutes later)
  CHECK(solar::moon_phase(2026, 268, 12 + 49 / 60.0f, kNyTz) == doctest::Approx(0.5f).epsilon(0.02));
  CHECK(solar::moon(kNyLat, kNyLon, kNyTz, 2026, 268, 18 + 15 / 60.0f).elevation < 0);
  const solar::Position rising = solar::moon(kNyLat, kNyLon, kNyTz, 2026, 268, 19.0f);
  CHECK(rising.elevation > 0);
  CHECK(rising.azimuth == doctest::Approx(90).epsilon(0.1));
  // new at 11:50 EDT on 2026-10-10: beside the sun at noon, at 37.4 / 168.3 degrees
  const float phase = solar::moon_phase(2026, 282, 11 + 50 / 60.0f, kNyTz);
  CHECK(std::min(phase, 1 - phase) < 0.005f);
  const solar::Position m = solar::moon(kNyLat, kNyLon, kNyTz, 2026, 282, 12.0f);
  CHECK(std::fabs(m.elevation - 37.4f) < 1);
  CHECK(std::fabs(m.azimuth - 168.3f) < 1);
  // waning gibbous on 2026-10-01: up at 21:40 (the almanac: 21:38), at 12.1 / 64.3 at 23:00
  CHECK(solar::moon_phase(2026, 273, 15.5f, kNyTz) == doctest::Approx(0.686f).epsilon(0.01));
  CHECK(solar::moon(kNyLat, kNyLon, kNyTz, 2026, 273, 21 + 25 / 60.0f).elevation < 0);
  CHECK(solar::moon(kNyLat, kNyLon, kNyTz, 2026, 273, 21 + 55 / 60.0f).elevation > 0);
  const solar::Position late = solar::moon(kNyLat, kNyLon, kNyTz, 2026, 273, 23.0f);
  CHECK(std::fabs(late.elevation - 12.1f) < 1);
  CHECK(std::fabs(late.azimuth - 64.3f) < 1);
}

TEST_CASE("themed: the weather's WMO code sets the horizon's cover and precipitation") {
  themed::Sky sky;
  faces::weather_to_sky(0, sky);  // clear
  CHECK_EQ(sky.cover, 0);
  CHECK(sky.precip == themed::Sky::Precip::None);
  faces::weather_to_sky(2, sky);  // partly cloudy
  CHECK_EQ(sky.cover, 1);
  faces::weather_to_sky(3, sky);  // overcast
  CHECK_EQ(sky.cover, 3);
  CHECK(sky.precip == themed::Sky::Precip::None);
  faces::weather_to_sky(61, sky);  // rain
  CHECK_EQ(sky.cover, 3);
  CHECK(sky.precip == themed::Sky::Precip::Rain);
  faces::weather_to_sky(80, sky);  // rain showers
  CHECK_EQ(sky.cover, 2);
  CHECK(sky.precip == themed::Sky::Precip::Rain);
  faces::weather_to_sky(71, sky);  // snow
  CHECK(sky.precip == themed::Sky::Precip::Snow);
  faces::weather_to_sky(95, sky);  // thunderstorm
  CHECK(sky.precip == themed::Sky::Precip::Rain);
}

TEST_CASE("themed: the words grid spells the time") {
  CHECK(themed::word_lit("IT", 10, 32));
  CHECK(themed::word_lit("IS", 10, 32));
  CHECK(themed::word_lit("HALF", 10, 32));
  CHECK(themed::word_lit("PAST", 10, 32));
  CHECK(themed::word_lit("TEN", 10, 32));
  CHECK(themed::word_lit("AM", 10, 32));
  CHECK_FALSE(themed::word_lit("PM", 10, 32));
  CHECK_FALSE(themed::word_lit("OCLOCK", 10, 32));
  CHECK_FALSE(themed::word_lit("TENm", 10, 32));
  CHECK_EQ(themed::word_dots(32), 2);
  // quarter to three in the afternoon: the next hour is named
  CHECK(themed::word_lit("QUARTER", 14, 45));
  CHECK(themed::word_lit("TO", 14, 45));
  CHECK(themed::word_lit("THREE", 14, 45));
  CHECK_FALSE(themed::word_lit("TWO", 14, 45));
  CHECK(themed::word_lit("PM", 14, 45));
  CHECK_EQ(themed::word_dots(45), 0);
  // midnight
  CHECK(themed::word_lit("TWELVE", 0, 0));
  CHECK(themed::word_lit("OCLOCK", 0, 0));
  CHECK(themed::word_lit("AM", 0, 0));
  // five to eight in the evening
  CHECK(themed::word_lit("FIVEm", 19, 58));
  CHECK(themed::word_lit("TO", 19, 58));
  CHECK(themed::word_lit("EIGHT", 19, 58));
  CHECK_FALSE(themed::word_lit("FIVE", 19, 58));
  CHECK_EQ(themed::word_dots(58), 3);
  // twenty-five to twelve at night names TWELVE and PM
  CHECK(themed::word_lit("TWENTY", 23, 36));
  CHECK(themed::word_lit("FIVEm", 23, 36));
  CHECK(themed::word_lit("TWELVE", 23, 36));
  CHECK(themed::word_lit("PM", 23, 36));
}

TEST_CASE("themed: the flip animates a minute change in ten frames of 45 ms, then rests") {
  p64::system::Settings s;
  s.clock.face = ClockFace::Flip;
  faces::ClockState state;
  Frame f, rest;
  tm t = moment(10, 32, 20);
  faces::ClockContext ctx;
  ctx.time = &t;
  CHECK_EQ(faces::draw_clock(rest, s, ctx, state), 40000u);
  CHECK_EQ(state.phase, 0);
  t = moment(10, 33, 0);
  Frame frames[10];
  for (int i = 0; i < 10; ++i) CHECK_EQ(faces::draw_clock(frames[i], s, ctx, state), themed::kFlipFrameMs);
  CHECK_EQ(state.phase, 0);
  CHECK_EQ(state.shown_minute, 33);
  // every frame differs from the next and from the resting faces
  for (int i = 0; i + 1 < 10; ++i) CHECK_FALSE(same(frames[i], frames[i + 1]));
  Frame after;
  CHECK_EQ(faces::draw_clock(after, s, ctx, state), 60000u);
  CHECK_FALSE(same(after, rest));
  for (int i = 0; i < 10; ++i) CHECK_FALSE(same(frames[i], after));
  // the mid-change frames keep the rest of the face: the date line is unchanged
  for (int i = 0; i < 10; ++i)
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 64; ++x) CHECK(frames[i].get(x, y) == rest.get(x, y));
  // a frame of the change matches the same frame drawn directly
  themed::Moment from = themed::Moment::from(moment(10, 32, 59)), to = themed::Moment::from(t);
  Frame direct;
  themed::draw_flip(direct, to, themed::Options{}, 4, &from);
  CHECK(same(direct, frames[3]));
}

TEST_CASE("themed: the flip's hour change turns both tiles, the hour one frame behind") {
  themed::Moment from = themed::Moment::from(moment(10, 59, 59)), to = themed::Moment::from(moment(11, 0, 0));
  Frame f1, f2, rest_from;
  themed::draw_flip(rest_from, from, themed::Options{});
  themed::draw_flip(f1, to, themed::Options{}, 1, &from);
  themed::draw_flip(f2, to, themed::Options{}, 2, &from);
  // frame 1: the hour tile (x 2..31) still shows 10, exactly as at rest
  for (int y = 15; y < 49; ++y)
    for (int x = 2; x < 32; ++x) CHECK(f1.get(x, y) == rest_from.get(x, y));
  // frame 2: the hour tile has started
  int diff = 0;
  for (int y = 15; y < 49; ++y)
    for (int x = 2; x < 32; ++x) diff += f2.get(x, y) != rest_from.get(x, y);
  CHECK(diff > 50);
}

TEST_CASE("themed: the hold times follow the seconds and blink settings, minus the milliseconds") {
  p64::system::Settings s;
  faces::ClockState state;
  Frame f;
  tm t = moment(13, 7, 20);
  faces::ClockContext ctx;
  ctx.time = &t;
  ctx.millis = 250;
  const auto hold = [&] { return faces::draw_clock(f, s, ctx, state); };
  s.clock.face = ClockFace::Words;
  CHECK_EQ(hold(), 39750u);
  s.clock.seconds = true;
  CHECK_EQ(hold(), 39750u);  // the words have no seconds element
  s.clock.face = ClockFace::Hourglass;
  CHECK_EQ(hold(), 750u);
  s.clock.face = ClockFace::Orrery;
  CHECK_EQ(hold(), 750u);
  s.clock.face = ClockFace::Flip;
  CHECK_EQ(hold(), 750u);
  s.clock.seconds = false;
  CHECK_EQ(hold(), 39750u);
  s.clock.face = ClockFace::Nixie;
  CHECK_EQ(hold(), 39750u);
  s.clock.blink_colon = true;
  CHECK_EQ(hold(), 750u);
  s.clock.face = ClockFace::Horizon;
  CHECK_EQ(hold(), 750u);
  s.clock.face = ClockFace::Orrery;
  CHECK_EQ(hold(), 750u);
  s.clock.face = ClockFace::Hourglass;
  CHECK_EQ(hold(), 39750u);  // no colon
  s.clock.face = ClockFace::Digital;
  CHECK_EQ(hold(), 750u);
  s.clock.blink_colon = false;
  CHECK_EQ(hold(), 39750u);
  s.clock.face = ClockFace::Analogue;
  CHECK_EQ(hold(), 39750u);
  // the LED is alive all the time: a pulse step, never past the second
  s.clock.face = ClockFace::Led;
  CHECK_EQ(hold(), 200u);
  ctx.millis = 900;
  CHECK_EQ(hold(), 100u);
  ctx.millis = 250;
  // never less than a millisecond, never more than a minute
  ctx.millis = 999;
  s.clock.face = ClockFace::Orrery;
  s.clock.seconds = true;
  CHECK_EQ(hold(), 1u);
}

TEST_CASE("themed: every face says so when the time is unknown") {
  p64::system::Settings s;
  for (int face = 0; face < 9; ++face) {
    s.clock.face = static_cast<ClockFace>(face);
    faces::ClockState state;
    state.phase = 5;
    Frame f;
    faces::ClockContext ctx;
    CHECK_EQ(faces::draw_clock(f, s, ctx, state), 1000u);
    CHECK(lit_count(f, 0, 0, 63, 63) > 20);
    CHECK_EQ(state.phase, 0);
  }
}

TEST_CASE("themed: the orrery's Earth starts at the top and is at three o'clock at three") {
  int ex, ey, mx, my, qx, qy;
  themed::orrery_positions(themed::Moment::from(moment(0, 0, 0)), ex, ey, mx, my, qx, qy);
  CHECK_EQ(ex, 32);
  CHECK_EQ(ey, 8);
  CHECK_EQ(mx, 32);
  CHECK_EQ(my, 4);  // the moon above the earth at minute 0
  CHECK_EQ(qx, 32);
  CHECK_EQ(qy, 18);
  themed::orrery_positions(themed::Moment::from(moment(3, 0, 0)), ex, ey, mx, my, qx, qy);
  CHECK_EQ(ex, 52);
  CHECK_EQ(ey, 28);
  themed::orrery_positions(themed::Moment::from(moment(6, 30, 15)), ex, ey, mx, my, qx, qy);
  CHECK(ey > 40);  // half past six: the earth low on the left of the bottom
  CHECK(ex < 32);
  CHECK(qx > 32);  // second 15: Mercury at three o'clock
}

TEST_CASE("themed: the hourglass's top bulb empties over the hour and the heap grows") {
  const auto sand_in = [](const Frame &f, int y0, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; ++y)
      for (int x = 6; x <= 24; ++x) {
        const Rgb c = f.get(x, y);
        if ((c.r == 232 && c.g == 184) || (c.r == 204 && c.g == 150)) ++n;
      }
    return n;
  };
  Frame a, b, c;
  themed::draw_hourglass(a, themed::Moment::from(moment(10, 0, 0)), themed::Options{});
  themed::draw_hourglass(b, themed::Moment::from(moment(10, 30, 0)), themed::Options{});
  themed::draw_hourglass(c, themed::Moment::from(moment(10, 59, 0)), themed::Options{});
  const int top_a = sand_in(a, 11, 31), top_b = sand_in(b, 11, 31), top_c = sand_in(c, 11, 31);
  CHECK(top_a > top_b);
  CHECK(top_b > top_c);
  CHECK(top_c < top_a / 10);
  const int bot_a = sand_in(a, 33, 52), bot_b = sand_in(b, 33, 52), bot_c = sand_in(c, 33, 52);
  CHECK(bot_a < bot_b);
  CHECK(bot_b < bot_c);
  // with the seconds on, the stream loses a grain every third row; off, it is whole
  Frame with, without;
  themed::Options o;
  o.seconds = true;
  themed::draw_hourglass(with, themed::Moment::from(moment(10, 30, 7)), o);
  themed::draw_hourglass(without, themed::Moment::from(moment(10, 30, 7)), themed::Options{});
  CHECK(sand_in(with, 32, 40) < sand_in(without, 32, 40));
}

TEST_CASE("themed: 12-hour mode blanks the leading zero and shows AM or PM on every face") {
  themed::Options h12;
  h12.h24 = false;
  const themed::Moment m = themed::Moment::from(moment(7, 5, 0));
  CHECK_EQ(themed::hour_text(m, h12), " 7");
  CHECK_EQ(themed::hour_text(m, themed::Options{}), "07");
  CHECK_EQ(themed::hour_text(themed::Moment::from(moment(0, 5, 0)), h12), "12");
  CHECK_EQ(themed::hour_text(themed::Moment::from(moment(12, 5, 0)), h12), "12");
  // each face draws something different in 12 h mode (the AM/PM mark)
  Frame a, b;
  themed::draw_flip(a, m, themed::Options{});
  themed::draw_flip(b, m, h12);
  CHECK_FALSE(same(a, b));
  themed::draw_nixie(a, m, themed::Options{});
  themed::draw_nixie(b, m, h12);
  CHECK_FALSE(same(a, b));
  themed::draw_hourglass(a, m, themed::Options{});
  themed::draw_hourglass(b, m, h12);
  CHECK_FALSE(same(a, b));
  themed::draw_orrery(a, m, themed::Options{});
  themed::draw_orrery(b, m, h12);
  CHECK_FALSE(same(a, b));
  themed::draw_horizon(a, m, themed::Options{}, themed::Sky{});
  themed::draw_horizon(b, m, h12, themed::Sky{});
  CHECK_FALSE(same(a, b));
  themed::draw_led(a, m, themed::Options{}, themed::LedStyle::Red);
  themed::draw_led(b, m, h12, themed::LedStyle::Red);
  CHECK_FALSE(same(a, b));
}

TEST_CASE("themed: the horizon is dark at night and bright by day, and the weather greys it") {
  Frame night, day, overcast;
  themed::Sky ny;
  ny.latitude = kNyLat, ny.longitude = kNyLon, ny.tz_hours = kNyTz;
  themed::draw_horizon(night, themed::Moment::from(moment(1, 0, 0)), themed::Options{}, ny);
  themed::draw_horizon(day, themed::Moment::from(moment(13, 0, 0)), themed::Options{}, ny);
  ny.cover = 3;
  ny.precip = themed::Sky::Precip::Rain;
  themed::draw_horizon(overcast, themed::Moment::from(moment(13, 0, 0)), themed::Options{}, ny);
  const auto sky_sum = [](const Frame &f) {
    long s = 0;
    for (int y = 20; y < 30; ++y)
      for (int x = 0; x < 64; ++x) s += f.get(x, y).r + f.get(x, y).g + f.get(x, y).b;
    return s;
  };
  CHECK(sky_sum(night) * 4 < sky_sum(day));
  // the overcast sky is less blue than the clear one
  const auto blue = [](const Frame &f) {
    long s = 0;
    for (int y = 20; y < 30; ++y)
      for (int x = 0; x < 64; ++x) s += f.get(x, y).b - f.get(x, y).r;
    return s;
  };
  CHECK(blue(overcast) < blue(day));
  // the sun is up by day (a gold pixel in the sky), not at night
  int gold_day = 0, gold_night = 0;
  for (int y = 0; y < 46; ++y)
    for (int x = 0; x < 64; ++x) {
      gold_day += day.get(x, y) == Rgb{255, 222, 90};
      gold_night += night.get(x, y) == Rgb{255, 222, 90};
    }
  CHECK(gold_day > 5);
  CHECK_EQ(gold_night, 0);
}

TEST_CASE("themed: settings JSON round-trips every face name") {
  static const char *const kNames[] = {"digital", "analogue", "flip", "nixie", "horizon", "words", "hourglass", "orrery", "led"};
  for (int i = 0; i < 9; ++i) {
    p64::system::Settings s;
    std::string error;
    const std::string json = std::string("{\"clock\":{\"face\":\"") + kNames[i] + "\"}}";
    CHECK(s.apply_json(json.c_str(), error));
    CHECK_EQ(static_cast<int>(s.clock.face), i);
    CHECK(s.to_json().find(std::string("\"face\":\"") + kNames[i] + "\"") != std::string::npos);
  }
  // an unknown name leaves the face alone
  p64::system::Settings s;
  std::string error;
  s.clock.face = ClockFace::Nixie;
  s.apply_json("{\"clock\":{\"face\":\"sundial\"}}", error);
  CHECK(s.clock.face == ClockFace::Nixie);
}

TEST_CASE("themed: the LED style round-trips and an unknown one is ignored") {
  static const char *const kStyles[] = {"red", "green", "amber", "blue", "vfd"};
  for (int i = 0; i < 5; ++i) {
    p64::system::Settings s;
    std::string error;
    const std::string json = std::string("{\"clock\":{\"led_style\":\"") + kStyles[i] + "\"}}";
    CHECK(s.apply_json(json.c_str(), error));
    CHECK_EQ(static_cast<int>(s.clock.led_style), i);
    CHECK(s.to_json().find(std::string("\"led_style\":\"") + kStyles[i] + "\"") != std::string::npos);
  }
  p64::system::Settings s;
  std::string error;
  s.clock.led_style = p64::system::LedStyle::Amber;
  s.apply_json("{\"clock\":{\"led_style\":\"pink\"}}", error);
  CHECK(s.clock.led_style == p64::system::LedStyle::Amber);
}

TEST_CASE("themed: the LED cross-fades a second change in five frames of 40 ms, then breathes") {
  p64::system::Settings s;
  s.clock.face = ClockFace::Led;
  s.clock.seconds = true;
  faces::ClockState state;
  Frame rest;
  tm t = moment(10, 32, 20);
  faces::ClockContext ctx;
  ctx.time = &t;
  CHECK_EQ(faces::draw_clock(rest, s, ctx, state), themed::kLedPulseStepMs);
  CHECK_EQ(state.phase, 0);
  CHECK_EQ(state.shown_second, 20);
  // the same second again: no change, another pulse step, the frame moves with the millis
  Frame later;
  ctx.millis = 600;
  CHECK_EQ(faces::draw_clock(later, s, ctx, state), 200u);
  CHECK_FALSE(same(later, rest));
  ctx.millis = 0;
  t = moment(10, 32, 21);
  Frame frames[themed::kLedFadeFrames];
  for (int i = 0; i < themed::kLedFadeFrames; ++i) {
    CHECK_EQ(faces::draw_clock(frames[i], s, ctx, state), themed::kLedFadeMs);
    ctx.millis += themed::kLedFadeMs;
  }
  CHECK_EQ(state.phase, 0);
  CHECK_EQ(state.shown_second, 21);
  for (int i = 0; i + 1 < themed::kLedFadeFrames; ++i) CHECK_FALSE(same(frames[i], frames[i + 1]));
  // the last frame is the new second at rest (the fade is complete at 255)
  Frame direct;
  themed::draw_led(direct, themed::Moment::from(t), themed::Options{true, false, true, false}, themed::LedStyle::Red, ctx.millis - themed::kLedFadeMs);
  CHECK(same(direct, frames[themed::kLedFadeFrames - 1]));
  CHECK_EQ(faces::draw_clock(direct, s, ctx, state), 200u);
  // with the seconds off, a new second is no change: no fade
  s.clock.seconds = false;
  t = moment(10, 32, 22);
  ctx.millis = 0;
  CHECK_EQ(faces::draw_clock(direct, s, ctx, state), 200u);
  CHECK_EQ(state.phase, 0);
  // the pulse: brightest on the four-second beat, dimmest two seconds later
  CHECK_EQ(themed::led_pulse(0), 255);
  CHECK_EQ(themed::led_pulse(2000), 140);
  CHECK_EQ(themed::led_pulse(4000), 255);
  CHECK_EQ(themed::led_ms(themed::Moment::from(moment(0, 0, 3)), 250), 3250);
  // the five styles all differ
  Frame styles[5];
  for (int i = 0; i < 5; ++i) themed::draw_led(styles[i], themed::Moment::from(t), themed::Options{}, static_cast<themed::LedStyle>(i));
  for (int i = 0; i < 5; ++i)
    for (int j = i + 1; j < 5; ++j) CHECK_FALSE(same(styles[i], styles[j]));
}

}  // namespace

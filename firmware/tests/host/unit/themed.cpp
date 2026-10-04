// Host unit tests: the themed clock faces (spec 7.1): the solar model against the almanac,
// the words grid, the flip's animation state, the hold times, the orrery's arithmetic, the
// hourglass's sand, the PNG pictures of the Horizon-RD and the aquarium and their cadence. The pixels themselves are compared with the mock's references by
// tests/host/run.py (tests/host/corpus/clock/).
#include "common.hpp"
#include "dial.hpp"
#include "faces.hpp"
#include "picture.hpp"
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

TEST_CASE("themed: the horizon's sun sets on the far hills' line at the almanac's sunset") {
  // New York, 2026-09-26 (the almanac: sunset 18:46 EDT). The disc rests on the hill line
  // until 18:40, slides behind it and is gone by 18:48 (the solar model runs about 0.3
  // degrees high: two minutes); the far hills stand 5 px above the horizon there and used
  // to hide it an hour early.
  themed::Sky ny;
  ny.latitude = kNyLat, ny.longitude = kNyLon, ny.tz_hours = kNyTz;
  const auto sun_pixels = [&](int h, int m) {
    Frame f;
    themed::draw_horizon(f, themed::Moment::from(moment(h, m)), themed::Options{}, ny);
    int n = 0;
    for (int y = 25; y < 46; ++y)
      for (int x = 40; x < 64; ++x) {
        const Rgb c = f.get(x, y);
        if (c.r > 220 && c.g > 90 && c.b < 160) ++n;  // the disc, red to gold
      }
    return n;
  };
  for (const int hm : {1700, 1740, 1800, 1820, 1840}) {
    CAPTURE(hm);
    CHECK(sun_pixels(hm / 100, hm % 100) >= 30);  // the whole 7 px disc
  }
  const int half = sun_pixels(18, 43);
  CHECK(half > 0);
  CHECK(half < 30);
  CHECK(sun_pixels(18, 48) == 0);
  CHECK(sun_pixels(19, 5) == 0);
}

TEST_CASE("themed: the horizon's bodies rest on the hill line, never dip while rising, and arc symmetrically") {
  for (const int radius : {3, 4}) {
    for (int x = 0; x < 64; ++x) {
      CAPTURE(radius);
      CAPTURE(x);
      CHECK(themed::horizon_body_row(-0.84, x, radius) == -1);
      int previous = 64;
      for (double el = -0.82; el <= 90; el += 0.05) {
        const int y = themed::horizon_body_row(el, x, radius);
        REQUIRE(y >= 0);
        CHECK(y <= previous);
        previous = y;
      }
      CHECK(themed::horizon_body_row(90, x, radius) == 18);
    }
    // above the first 5 degrees the arc is the same in every column (round, symmetric)
    for (const double el : {6.0, 15.0, 30.0, 60.0})
      for (int x = 1; x < 64; ++x) CHECK(themed::horizon_body_row(el, x, radius) == themed::horizon_body_row(el, 0, radius));
  }
  // at rest the disc's bottom row is just above the hill line, at the hidden end its top is on it
  CHECK(themed::horizon_body_row(0.7, 57, 3) == 41 - 3 - 1);
  CHECK(themed::horizon_body_row(-0.8, 57, 3) == 41 + 3);
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
  static const char *const kNames[] = {"digital", "analogue", "flip", "nixie", "horizon", "words", "hourglass", "orrery", "led", "horizon_rd", "aquarium", "bracket", "station"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == p64::system::kClockFaceCount);
  for (int i = 0; i < p64::system::kClockFaceCount; ++i) {
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

TEST_CASE("picture: every embedded PNG decodes to its size with a 0/255 alpha") {
  namespace assets = p64::widgets::assets;
  namespace picture = p64::widgets::picture;
  struct Case {
    const assets::Png *png;
    int w, h;
    bool all_opaque, some_clear;
  };
  const Case cases[] = {
      {&assets::kAquariumTankPng, 1024, 64, true, false},   {&assets::kAquariumFrontPng, 1024, 64, false, true},
      {&assets::kAquariumSignPng, 27, 10, true, false},     {&assets::kAquariumLightsPng, 64, 64, false, true},
      {&assets::kAquariumFishAPng, 160, 17, false, true},   {&assets::kAquariumFishBPng, 208, 19, false, true},
      {&assets::kAquariumFishCPng, 112, 10, false, true},   {&assets::kHorizonRdLandPng, 64, 38, false, true},
      {&assets::kHorizonRdLakePng, 64, 38, false, true},    {&assets::kHorizonRdLightsPng, 64, 38, false, true},
      {&assets::kHorizonRdSunPng, 12, 12, false, true},     {&assets::kHorizonRdMoonPng, 12, 12, false, true},
      {&assets::kHorizonRdCloudAPng, 14, 8, false, true},   {&assets::kHorizonRdCloudBPng, 18, 9, false, true},
  };
  for (const Case &c : cases) {
    picture::Picture p;
    REQUIRE(picture::decode(*c.png, p));
    CHECK_EQ(p.w, c.w);
    CHECK_EQ(p.h, c.h);
    REQUIRE_EQ(p.rgba.size(), static_cast<size_t>(c.w) * c.h * 4);
    int opaque = 0, clear = 0, other = 0;
    for (size_t i = 3; i < p.rgba.size(); i += 4) (p.rgba[i] == 255 ? opaque : p.rgba[i] == 0 ? clear : other)++;
    CHECK_EQ(other, 0);
    CHECK(opaque > 0);
    CHECK_EQ(clear == 0, c.all_opaque);
    CHECK_EQ(clear > 0, c.some_clear);
  }
  // the sign's first pixel is the picture's (a wooden brown), not a blend
  picture::Picture sign;
  REQUIRE(picture::decode(assets::kAquariumSignPng, sign));
  CHECK(sign.view().colour(13, 5).r > sign.view().colour(13, 5).b);
  // a file that is not a PNG is refused and leaves nothing behind
  static const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  picture::Picture none;
  CHECK_FALSE(picture::decode(assets::Png{junk, sizeof(junk)}, none));
  CHECK_FALSE(none.ok());
}

TEST_CASE("themed: the Horizon-RD loads its art once, moves its glints every 500 ms and frees the art for another face") {
  p64::system::Settings s;
  s.clock.face = ClockFace::HorizonRd;
  faces::ClockState state;
  faces::ClockContext ctx;
  tm t = moment(13, 0, 0);
  ctx.time = &t;
  Frame a, b, c;
  CHECK_EQ(faces::draw_clock(a, s, ctx, state), 500u);
  REQUIRE(state.horizon_rd);
  const themed::HorizonRdArt *art = state.horizon_rd.get();
  ctx.millis = 130;
  CHECK_EQ(faces::draw_clock(b, s, ctx, state), 370u);
  CHECK(same(a, b));  // the same half second: the same glints
  CHECK_EQ(state.horizon_rd.get(), art);  // not decoded again
  ctx.millis = 500;
  CHECK_EQ(faces::draw_clock(c, s, ctx, state), 500u);
  CHECK_FALSE(same(a, c));
  // only the lake moved
  for (int y = 0; y < 51; ++y)
    for (int x = 0; x < 64; ++x) REQUIRE(a.get(x, y) == c.get(x, y));
  // night: the land is darker and the cabin's windows are lit in their own colour
  Frame night;
  tm late = moment(1, 0, 0);
  ctx.time = &late;
  ctx.millis = 0;
  faces::draw_clock(night, s, ctx, state);
  const auto land_sum = [](const Frame &f) {
    long sum = 0;
    for (int y = 40; y < 56; ++y)
      for (int x = 0; x < 64; ++x) sum += f.get(x, y).r + f.get(x, y).g + f.get(x, y).b;
    return sum;
  };
  CHECK(land_sum(night) * 3 < land_sum(a));
  CHECK(night.get(11, 55) == Rgb{255, 206, 96});
  CHECK_FALSE(a.get(11, 55) == Rgb{255, 206, 96});
  // the sky differs with the weather, and the skyline hides a body: the sun's row is in the sky
  themed::HorizonRdArt own;
  REQUIRE(own.load());
  CHECK(own.raised > 30);
  CHECK(own.raised < 40);
  // another face: the art goes
  s.clock.face = ClockFace::Nixie;
  faces::draw_clock(b, s, ctx, state);
  CHECK_FALSE(state.horizon_rd);
}

TEST_CASE("themed: the aquarium draws a frame every 150 ms counted from midnight, the time on its sign, a blue night") {
  p64::system::Settings s;
  s.clock.face = ClockFace::Aquarium;
  faces::ClockState state;
  faces::ClockContext ctx;
  tm t = moment(10, 32, 37);  // 37 000 ms into the minute: 100 ms into a tank frame
  ctx.time = &t;
  Frame a, b, c;
  CHECK_EQ(faces::draw_clock(a, s, ctx, state), 50u);
  REQUIRE(state.aquarium);
  CHECK_FALSE(state.horizon_rd);
  ctx.millis = 50;
  CHECK_EQ(faces::draw_clock(b, s, ctx, state), 150u);
  CHECK_FALSE(same(a, b));  // the next frame of the tank
  ctx.millis = 60;
  CHECK_EQ(faces::draw_clock(c, s, ctx, state), 140u);
  // the sign: the ink's colour spells the time and is the same pixels by night
  const Rgb ink{255, 240, 200};
  const auto ink_at = [&ink](const Frame &f, int x, int y) { return f.get(x, y) == ink; };
  int day_ink = 0;
  for (int y = 19; y < 29; ++y)
    for (int x = 27; x < 54; ++x) day_ink += ink_at(a, x, y);
  CHECK(day_ink > 30);
  // (the same clock time by night at Greenwich and by day half a world away)
  Frame night, far_day;
  tm late = moment(22, 32, 37);
  ctx.time = &late;
  ctx.millis = 0;
  faces::draw_clock(night, s, ctx, state);
  ctx.sky.longitude = 180;
  faces::draw_clock(far_day, s, ctx, state);
  ctx.sky.longitude = 0;
  CHECK_FALSE(same(night, far_day));
  for (int y = 19; y < 29; ++y)
    for (int x = 27; x < 54; ++x) REQUIRE_EQ(ink_at(night, x, y), ink_at(far_day, x, y));
  long day_sum = 0, night_sum = 0, night_blue = 0, night_red = 0;
  for (int y = 4; y < 18; ++y)
    for (int x = 8; x < 24; ++x) {
      day_sum += a.get(x, y).r + a.get(x, y).g + a.get(x, y).b;
      night_sum += night.get(x, y).r + night.get(x, y).g + night.get(x, y).b;
      night_blue += night.get(x, y).b;
      night_red += night.get(x, y).r;
    }
  CHECK(night_sum * 3 < day_sum * 2);
  CHECK(night_blue > night_red * 2);
  // the castle's window glows at night only
  CHECK(night.get(14, 34).r > 200);
  CHECK(a.get(14, 34).r < 160);
  // 12 h: the time only, no leading zero, centred on the board
  s.clock.h24 = false;
  tm morning = moment(9, 5, 0);
  ctx.time = &morning;
  Frame twelve;
  faces::draw_clock(twelve, s, ctx, state);
  int left = 64, right = -1;
  for (int y = 19; y < 29; ++y)
    for (int x = 27; x < 54; ++x)
      if (ink_at(twelve, x, y)) left = std::min(left, x), right = std::max(right, x);
  CHECK(left >= 31);  // "9:05" is narrower than "10:32"
  CHECK(std::abs((left - 27) - (53 - right)) <= 2);
  // another face: the tank's 262 KB go
  s.clock.face = ClockFace::Digital;
  faces::draw_clock(b, s, ctx, state);
  CHECK_FALSE(state.aquarium);
}

TEST_CASE("dial: profiles, floor division and directions are the mock's integers") {
  namespace dial = themed::dial;
  CHECK_EQ(dial::floor_div(7, 2), 3);
  CHECK_EQ(dial::floor_div(-7, 2), -4);
  CHECK_EQ(dial::floor_div(-8, 2), -4);
  CHECK_EQ(dial::floor_div(0, 5), 0);
  static constexpr dial::Point pts[] = {{-256, 64}, {512, 64}, {736, 198}, {960, 19}, {992, 19}};
  const dial::Profile p = dial::profile(pts);
  CHECK_EQ(dial::profile_width(p, -257), -1);
  CHECK_EQ(dial::profile_width(p, -256), 64);
  CHECK_EQ(dial::profile_width(p, 100), 64);
  CHECK_EQ(dial::profile_width(p, 624), 131);  // half way up the spade
  CHECK_EQ(dial::profile_width(p, 848), 108);  // half way down it: 198 + floor(-179 * 112 / 224)
  CHECK_EQ(dial::profile_width(p, 992), 19);
  CHECK_EQ(dial::profile_width(p, 993), -1);
  CHECK_EQ(dial::sin_q(900), 16384);
  CHECK_EQ(dial::sin_q(-900), -16384);
  CHECK_EQ(dial::cos_q(3600), 16384);
  int x, y;
  dial::polar_px(63, 63, 0, 50, x, y);  // 25 px above 31.5
  CHECK_EQ(x, 32);
  CHECK_EQ(y, 7);
  dial::polar_px(63, 63, 900, 50, x, y);
  CHECK_EQ(x, 57);
  CHECK_EQ(y, 32);
  // a flat shape straight up from a centre on a pixel: its half width of one pixel on
  // each side, the edge included, and its ten pixels of length
  Frame f;
  static constexpr dial::Point bar[] = {{0, 64}, {640, 64}};
  dial::draw_shape(f, 64, 64, 0, dial::profile(bar), dial::Shade{Rgb{9, 9, 9}});
  for (int x = 31; x <= 33; ++x) CHECK(f.get(x, 27) == Rgb{9, 9, 9});
  CHECK(f.get(30, 27) == Rgb{0, 0, 0});
  CHECK(f.get(34, 27) == Rgb{0, 0, 0});
  CHECK(f.get(32, 22) == Rgb{9, 9, 9});
  CHECK(f.get(32, 21) == Rgb{0, 0, 0});
  CHECK(f.get(32, 33) == Rgb{0, 0, 0});  // nothing behind the pivot
}

TEST_CASE("themed: the bracket clock swings its bob every two seconds and ticks its second hand with the setting") {
  p64::system::Settings s;
  s.clock.face = ClockFace::Bracket;
  faces::ClockState state;
  faces::ClockContext ctx;
  tm t = moment(10, 9, 36);
  ctx.time = &t;
  Frame a, b;
  CHECK_EQ(faces::draw_clock(a, s, ctx, state), 100u);
  ctx.millis = 130;
  CHECK_EQ(faces::draw_clock(b, s, ctx, state), 70u);
  // the bob: in the middle at the even second, at its right end half a second later, back
  // in the middle, at its left end, and the same two seconds on
  int x0, y0, x1, y1, x2, y2, x3, y3;
  themed::bracket_bob(themed::Moment::from(moment(0, 0, 0)), 0, x0, y0);
  themed::bracket_bob(themed::Moment::from(moment(0, 0, 0)), 500, x1, y1);
  themed::bracket_bob(themed::Moment::from(moment(0, 0, 1)), 0, x2, y2);
  themed::bracket_bob(themed::Moment::from(moment(0, 0, 1)), 500, x3, y3);
  CHECK_EQ(x0, 63);
  CHECK_EQ(y0, 63 - 23);
  CHECK(x1 > 63 + 7);
  CHECK_EQ(x2, 63);
  CHECK_EQ(x3, 63 - (x1 - 63));
  CHECK_EQ(y1, y3);
  themed::bracket_bob(themed::Moment::from(moment(0, 0, 2)), 0, x1, y1);
  CHECK_EQ(x1, x0);
  // the frame half a second on differs only where the bob swings (under the XII)
  Frame c;
  ctx.millis = 500;
  faces::draw_clock(c, s, ctx, state);
  ctx.millis = 0;
  CHECK_FALSE(same(a, c));
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      if (!(a.get(x, y) == c.get(x, y))) {
        REQUIRE(y >= 17);
        REQUIRE(y <= 24);
        REQUIRE(x >= 22);
        REQUIRE(x <= 41);
      }
  // the second hand is the setting's
  s.clock.seconds = true;
  Frame with;
  faces::draw_clock(with, s, ctx, state);
  int red = 0, red_without = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) red += with.get(x, y) == Rgb{176, 34, 30}, red_without += a.get(x, y) == Rgb{176, 34, 30};
  CHECK(red > 20);
  CHECK_EQ(red_without, 0);
  // the date follows the order setting
  s.clock.month_first = true;
  Frame other;
  faces::draw_clock(other, s, ctx, state);
  CHECK_FALSE(same(with, other));
}

TEST_CASE("themed: the station clock's second hand goes round in 58.5 s and waits; the glint sets the cadence") {
  const auto at = [](int sec) { return themed::Moment::from(moment(10, 9, sec)); };
  CHECK_EQ(themed::station_second_angle10(at(0), 0), 0);
  CHECK_EQ(themed::station_second_angle10(at(29), 250), 1800);  // half way at 29.25 s
  CHECK_EQ(themed::station_second_angle10(at(58), 400), 3593);
  CHECK_EQ(themed::station_second_angle10(at(58), 500), 0);  // at the top
  CHECK_EQ(themed::station_second_angle10(at(59), 900), 0);  // and waiting
  // the glint's cycle: from the hour, every twelve seconds
  CHECK_EQ(themed::station_glint_ms(themed::Moment::from(moment(10, 0, 0)), 0), 0);
  CHECK_EQ(themed::station_glint_ms(themed::Moment::from(moment(10, 9, 37)), 300), 1300);
  CHECK_EQ(themed::station_glint_ms(themed::Moment::from(moment(10, 9, 47)), 0), 11000);
  p64::system::Settings s;
  s.clock.face = ClockFace::Station;
  faces::ClockState state;
  faces::ClockContext ctx;
  Frame rest, glint, later;
  // without the seconds: at rest until the next glint, then a frame every 100 ms
  tm t = moment(10, 9, 41);
  ctx.time = &t;
  ctx.millis = 250;
  CHECK_EQ(faces::draw_clock(rest, s, ctx, state), 12000u - 5250u);
  t = moment(10, 9, 48);
  ctx.millis = 0;
  CHECK_EQ(faces::draw_clock(glint, s, ctx, state), 100u);
  ctx.millis = 730;
  CHECK_EQ(faces::draw_clock(glint, s, ctx, state), 70u);
  CHECK_FALSE(same(rest, glint));
  // the glint only lightens, and only on the dial
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      REQUIRE(glint.get(x, y).r >= rest.get(x, y).r);
      const int dx = 2 * x - 64, dy = 2 * y - 63;
      if (dx * dx + dy * dy > 57 * 57) REQUIRE(glint.get(x, y) == rest.get(x, y));
    }
  t = moment(10, 9, 49);
  ctx.millis = 500;
  CHECK_EQ(faces::draw_clock(later, s, ctx, state), 12000u - 1500u);
  CHECK(same(rest, later));  // over: the dial as it was
  // with the seconds: always a frame every 100 ms, and the red hand is there
  s.clock.seconds = true;
  ctx.millis = 40;
  CHECK_EQ(faces::draw_clock(later, s, ctx, state), 60u);
  int red = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) red += later.get(x, y) == Rgb{222, 30, 34};
  CHECK(red > 30);
}

}  // namespace

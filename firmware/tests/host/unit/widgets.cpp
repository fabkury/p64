// Host unit tests: the clock text, the weather model and the analogue face.
#include "common.hpp"
#include "faces.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


TEST_CASE("clock_format") {
  using namespace p64::widgets::clock_format;
  tm t = {};
  t.tm_hour = 14;
  t.tm_min = 5;
  t.tm_sec = 9;
  t.tm_wday = 6;
  t.tm_mday = 19;
  t.tm_mon = 8;
  CHECK(time_text(t, true, false) == "14:05");
  CHECK(time_text(t, true, true) == "14:05:09");
  CHECK(time_text(t, false, false) == " 2:05");
  CHECK(time_text(t, true, false, false) == "14 05");
  CHECK(meridiem(t, false) == "PM");
  CHECK(meridiem(t, true) == "");
  t.tm_hour = 0;
  CHECK(time_text(t, false, false) == "12:05");
  CHECK(meridiem(t, false) == "AM");
  CHECK(date_text(t, false) == "Sat 19 Sep");
  CHECK(date_text(t, true) == "Sat Sep 19");
}


TEST_CASE("weather_model") {
  using namespace p64::widgets::weather_model;
  using p64::widgets::icons::Group;
  using p64::widgets::icons::group_for_code;
  CHECK(group_for_code(0) == Group::Clear);
  CHECK(group_for_code(2) == Group::Partly);
  CHECK(group_for_code(3) == Group::Overcast);
  CHECK(group_for_code(45) == Group::Fog);
  CHECK(group_for_code(53) == Group::Drizzle);
  CHECK(group_for_code(63) == Group::Rain);
  CHECK(group_for_code(66) == Group::Freezing);
  CHECK(group_for_code(75) == Group::Snow);
  CHECK(group_for_code(81) == Group::Showers);
  CHECK(group_for_code(86) == Group::SnowShowers);
  CHECK(group_for_code(95) == Group::Thunder);
  CHECK(group_for_code(99) == Group::Hail);
  CHECK_EQ(weekday_of("2026-09-19"), 6);  // a Saturday
  CHECK_EQ(weekday_of("2024-01-15"), 1);  // a Monday
  CHECK_EQ(weekday_of("nope"), -1);
  const std::string url = request_url(48.85f, 2.35f, true);
  CHECK(url.find("latitude=48.8500") != std::string::npos);
  CHECK(url.find("temperature_unit=fahrenheit") != std::string::npos);
  CHECK(request_url(0, 0, false).find("fahrenheit") == std::string::npos);
  const char *reply =
      "{\"current_units\":{\"temperature_2m\":\"\xC2\xB0\x43\"},"
      "\"current\":{\"temperature_2m\":21.4,\"relative_humidity_2m\":55,\"weather_code\":61,\"is_day\":0},"
      "\"daily\":{\"time\":[\"2026-09-19\",\"2026-09-20\",\"2026-09-21\",\"2026-09-22\"],"
      "\"weather_code\":[61,2,0,95],\"temperature_2m_max\":[24,22,20,18],\"temperature_2m_min\":[15,14,12,11]}}";
  Forecast f;
  std::string e;
  CHECK(parse(reply, std::strlen(reply), f, e));
  CHECK(f.valid);
  CHECK(!f.imperial);
  CHECK((f.temperature > 21.3f && f.temperature < 21.5f));
  CHECK_EQ(f.humidity, 55);
  CHECK_EQ(f.code, 61);
  CHECK(!f.is_day);
  CHECK_EQ(f.today.weekday, 6);
  CHECK((f.today.max == 24.0f && f.today.min == 15.0f));
  CHECK_EQ(f.day_count, 3);
  CHECK_EQ(f.days[0].weekday, 0);
  CHECK_EQ(f.days[2].code, 95);
  CHECK(!parse("{\"reason\":\"bad\"}", 16, f, e));
  CHECK(e == "bad");
  CHECK(!parse("garbage", 7, f, e));
}


// --- the analogue clock face (spec 7.1) ------------------------------------------------

int lit_in(const Frame &f, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      if (f.get(x, y) != p64::gfx::kBlack) ++n;
  return n;
}

TEST_CASE("analogue") {
  using namespace p64::widgets::analogue;
  int x, y;
  hand_end(0.0f, 20, x, y);
  CHECK_EQ(x, 32); CHECK_EQ(y, 12);          // 12 o'clock: straight up from the centre
  hand_end(90.0f, 20, x, y);
  CHECK_EQ(x, 52); CHECK_EQ(y, 32);          // 3 o'clock: right
  hand_end(180.0f, 20, x, y);
  CHECK_EQ(x, 32); CHECK_EQ(y, 52);
  hand_end(270.0f, 20, x, y);
  CHECK_EQ(x, 12); CHECK_EQ(y, 32);
  Style st;
  st.ink = Rgb{255, 255, 255};
  st.background = p64::gfx::kBlack;
  Frame f;
  tm t = {};
  t.tm_hour = 3; t.tm_min = 0; t.tm_sec = 0; t.tm_mday = 19; t.tm_mon = 8; t.tm_wday = 6;
  draw(f, st, t);
  // 3:00: the hour hand runs right from the hub, the minute hand up; nothing points left or down beyond the date.
  CHECK(lit_in(f, 33, 30, 44, 33) >= 12);    // hour hand, 2 px thick, to the right
  CHECK(lit_in(f, 31, 9, 32, 30) >= 20);     // minute hand up (column 31/32)
  CHECK_EQ(lit_in(f, 12, 30, 28, 33), 0);    // nothing on the 9 o'clock arm except the numeral further out
  CHECK(lit_in(f, 26, 0, 37, 10) > 0);       // the "12" numeral and its tick
  CHECK(lit_in(f, 26, 53, 37, 63) > 0);      // the "6"
  CHECK(lit_in(f, 0, 27, 10, 37) > 0);       // the "9"
  CHECK(lit_in(f, 53, 27, 63, 37) > 0);      // the "3"
  CHECK(lit_in(f, 20, 41, 43, 47) > 0);      // the date under the centre
  CHECK((f.get(31, 31) != p64::gfx::kBlack && f.get(32, 32) != p64::gfx::kBlack));  // the hub
  // No second hand without the setting; with it, an accent-coloured pixel appears.
  st.seconds = true;
  t.tm_sec = 45;                              // 9 o'clock direction
  Frame g;
  draw(g, st, t);
  bool accent = false;
  for (int xx = 8; xx < 28; ++xx) if (g.get(xx, 31) == st.accent) accent = true;
  CHECK(accent);
  // Determinism: the same instant draws the same pixels.
  Frame h;
  draw(h, st, t);
  CHECK(std::memcmp(g.data(), h.data(), Frame::bytes()) == 0);
  // 6:30: the hour hand sits between 6 and 7 (below and slightly left), the minute hand straight down.
  t.tm_hour = 6; t.tm_min = 30; t.tm_sec = 0;
  st.seconds = false;
  Frame k;
  draw(k, st, t);
  CHECK(lit_in(k, 31, 34, 32, 54) >= 18);    // minute hand down
  hand_end(6 * 30.0f + 15.0f, kHourHand, x, y);
  CHECK((x < 32 && y > 40));
}

// --- the faces: clock, weather, temperature (faces.cpp) ----------------------------

namespace faces = p64::widgets::faces;

int lit_rect(const Frame &f, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const Rgb c = f.get(x, y);
      if (c.r || c.g || c.b) ++n;
    }
  return n;
}

tm at(int h, int m, int sec) {
  tm t{};
  t.tm_year = 126;
  t.tm_mon = 8;
  t.tm_mday = 22;
  t.tm_wday = 2;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = sec;
  return t;
}

TEST_CASE("faces: the digital clock holds until the next minute, or second when seconds show") {
  p64::system::Settings s;
  Frame f;
  const tm t = at(13, 7, 20);
  CHECK_EQ(faces::draw_clock(f, s, &t), 40000u);
  CHECK(lit_rect(f, 0, 0, 63, 63) > 40);
  s.clock.seconds = true;
  CHECK_EQ(faces::draw_clock(f, s, &t), 1000u);
  s.clock.seconds = false;
  s.clock.blink_colon = true;
  CHECK_EQ(faces::draw_clock(f, s, &t), 1000u);
}

TEST_CASE("faces: the clock without a synced time says so") {
  p64::system::Settings s;
  Frame f;
  CHECK_EQ(faces::draw_clock(f, s, nullptr), 1000u);
  CHECK(lit_rect(f, 0, 0, 63, 63) > 20);
}

TEST_CASE("faces: the widest clock (seconds, 12 h, scale 3) shrinks to fit the panel") {
  p64::system::Settings s;
  s.clock.scale = 3;
  s.clock.seconds = true;
  s.clock.h24 = false;
  Frame f;
  const tm t = at(23, 58, 59);
  faces::draw_clock(f, s, &t);
  CHECK_EQ(lit_rect(f, 0, 0, 0, 63), 0);    // a free column at each edge
  CHECK_EQ(lit_rect(f, 63, 0, 63, 63), 0);
  s.clock.analogue = true;
  CHECK_EQ(faces::draw_clock(f, s, &t), 1000u);  // the analogue face with its second hand
}

p64::widgets::weather_model::Forecast forecast(float low) {
  p64::widgets::weather_model::Forecast f;
  f.valid = true;
  f.fetched_us = 1000;
  f.temperature = 21.4f;
  f.code = 3;
  f.today = {2, 3, 24.0f, 15.0f};
  f.day_count = 3;
  for (int i = 0; i < 3; ++i) f.days[i] = {3 + i, 61, 18.0f, low};
  return f;
}

TEST_CASE("faces: the weather asks for a location, then shows the forecast, then no data after 6 h") {
  p64::system::Settings s;
  Frame f;
  faces::draw_weather(f, s, forecast(9), "", 2000);
  const int unset = lit_rect(f, 0, 0, 63, 63);
  CHECK(unset > 20);  // "WEATHER / SET A / LOCATION"
  s.weather.location_set = true;
  faces::draw_weather(f, s, forecast(9), "", 2000);
  CHECK(lit_rect(f, 2, 3, 25, 26) > 20);  // the icon
  CHECK(lit_rect(f, 0, 50, 63, 63) > 20); // the three-day strip
  faces::draw_weather(f, s, forecast(9), "HTTP 500", 1000 + faces::kWeatherStaleUs + 1);
  CHECK_EQ(lit_rect(f, 0, 53, 63, 63), 0);  // "NO DATA" and the error (rows 44-51), no strip
}

TEST_CASE("faces: the weather strip's low temperatures fit above the bottom edge (M7)") {
  // The low temperatures clipped at the panel's bottom row once; they start at row 57.
  CHECK(57 + p64::gfx::fonts::cap_height(p64::gfx::fonts::default_font(), 1) <= 64);
  p64::system::Settings s;
  s.weather.location_set = true;
  Frame f;
  faces::draw_weather(f, s, forecast(-12), "", 2000);  // the widest low
  CHECK(lit_rect(f, 0, 57, 63, 63) > 10);
  for (int col = 0; col < 3; ++col) CHECK(lit_rect(f, col * 21, 57, col * 21 + 20, 63) > 0);
}

TEST_CASE("faces: the temperature reading, in Fahrenheit when asked, with the trend arrow") {
  p64::system::Settings s;
  Frame c, fh, down;
  p64::widgets::Reading r;
  faces::draw_temperature(c, s, r);
  CHECK(lit_rect(c, 0, 0, 63, 63) > 10);  // "NO SENSOR"
  r.valid = true;
  r.temperature_c = 20.0f;
  r.humidity = 45.0f;
  r.trend_c_per_hour = 1.0f;
  faces::draw_temperature(c, s, r);
  s.weather.imperial = true;
  faces::draw_temperature(fh, s, r);
  CHECK(lit_rect(c, 0, 10, 63, 28) != lit_rect(fh, 0, 10, 63, 28));  // 20.0 C against 68.0 F
  r.trend_c_per_hour = -1.0f;
  faces::draw_temperature(down, s, r);
  bool differ = false;
  for (int y = 46; y < 51 && !differ; ++y)
    for (int x = 29; x < 34; ++x) differ |= !(fh.get(x, y) == down.get(x, y));
  CHECK(differ);  // the arrow turns
  s.temperature.trend = false;
  faces::draw_temperature(down, s, r);
  CHECK_EQ(lit_rect(down, 29, 46, 33, 50), 0);
}

}  // namespace

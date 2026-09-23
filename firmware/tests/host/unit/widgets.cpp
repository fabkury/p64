// Host unit tests: the clock text, the weather model and the analogue face.
#include "common.hpp"
#include "faces.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>

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

// --- the clock overlay (spec 6.1) -------------------------------------------------------

// The overlay as the player gets it: built once, stamped on the frame.
void overlay_on(Frame &f, const p64::system::Settings &s, const tm &t) {
  auto sprite = std::make_unique<faces::OverlaySprite>();
  faces::build_overlay(*sprite, s, t);
  faces::stamp_overlay(f, *sprite);
}

TEST_CASE("faces: the overlay key changes with every setting that shapes the drawing") {
  // The key once ignored the font: a font chosen in the web UI showed only at the next
  // minute (2026-09-23).
  p64::system::Settings s;
  const tm t = at(13, 7, 20);
  const uint32_t base = faces::overlay_key(s, t);
  CHECK(base != 0);
  CHECK_EQ(faces::overlay_key(s, at(13, 7, 59)), base);  // the seconds do not show
  CHECK(faces::overlay_key(s, at(13, 8, 0)) != base);
  auto differs = [&](auto change) {
    p64::system::Settings c = s;
    change(c);
    return faces::overlay_key(c, t) != base;
  };
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.font = "everyday-standard"; }));
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.corner = p64::system::Corner::BottomRight; }));
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.h24 = false; }));
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.colour = Rgb{255, 0, 0}; }));
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.border = false; }));
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.border_colour = Rgb{0, 0, 1}; }));
  CHECK(differs([](p64::system::Settings &c) { c.clock_overlay.border_opacity = 254; }));
  // An unknown font and a font not offered for the overlay both draw the default one.
  CHECK(!differs([](p64::system::Settings &c) { c.clock_overlay.font = "nope"; }));
  CHECK(!differs([](p64::system::Settings &c) { c.clock_overlay.font = "high-birth"; }));
}

TEST_CASE("faces: the overlay draws in its corner, with the border only when asked") {
  p64::system::Settings s;
  s.clock_overlay.colour = Rgb{200, 100, 50};
  const tm t = at(23, 59, 0);
  const Rgb grey{90, 90, 90};
  auto count = [](const Frame &f, Rgb c) {
    int n = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) n += f.get(x, y) == c ? 1 : 0;
    return n;
  };
  for (const char *font : {"capital-hill", "everyday-slight", "everyday-standard", "everyday-typical", "everyday-ample"}) {
    CAPTURE(font);
    s.clock_overlay.font = font;
    s.clock_overlay.border = true;
    s.clock_overlay.corner = p64::system::Corner::TopLeft;
    Frame f;
    f.clear(grey);
    overlay_on(f, s, t);
    CHECK(count(f, s.clock_overlay.colour) > 10);
    CHECK(count(f, Rgb{0, 0, 0}) > 10);
    int bottom_half = 0;
    for (int y = 32; y < 64; ++y)
      for (int x = 0; x < 64; ++x) bottom_half += f.get(x, y) == grey ? 0 : 1;
    CHECK_EQ(bottom_half, 0);
    s.clock_overlay.border = false;
    f.clear(grey);
    overlay_on(f, s, t);
    CHECK_EQ(count(f, Rgb{0, 0, 0}), 0);
    // Bottom right: the text stays inside the panel with its margin, none in the top half.
    s.clock_overlay.border = true;
    s.clock_overlay.corner = p64::system::Corner::BottomRight;
    f.clear(grey);
    overlay_on(f, s, t);
    int top_half = 0, edge = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        if (f.get(x, y) == grey) continue;
        if (y < 32) ++top_half;
        if (x == 63 || y == 63) ++edge;
      }
    CHECK_EQ(top_half, 0);
    CHECK_EQ(edge, 0);
  }
}

// An artwork-like background with black and bright pixels in it.
void artwork(Frame &f) {
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      f.set(x, y, Rgb{static_cast<uint8_t>(x * 4), static_cast<uint8_t>(y * 4), static_cast<uint8_t>((x ^ y) & 1 ? 0 : 99)});
}

TEST_CASE("faces: the cached overlay stamps the same pixels as the font drawing, border colour included") {
  // The overlay is drawn once per minute into an OverlaySprite and stamped on every frame
  // (2026-09-23: redrawing it per frame cost about 380 us on the device).
  auto sprite = std::make_unique<faces::OverlaySprite>();
  const tm t = at(23, 58, 0);
  const p64::system::Corner corners[] = {p64::system::Corner::TopLeft, p64::system::Corner::TopRight,
                                         p64::system::Corner::BottomLeft, p64::system::Corner::BottomRight};
  for (size_t i = 0; i < p64::gfx::fonts::kFontCount; ++i) {
    const p64::gfx::fonts::Font &font = *p64::gfx::fonts::kFonts[i];
    if (!font.overlay) continue;
    for (const p64::system::Corner corner : corners) {
      for (const bool border : {true, false}) {
        for (const bool h24 : {true, false}) {
          p64::system::Settings s;
          s.clock_overlay.font = font.name;
          s.clock_overlay.corner = corner;
          s.clock_overlay.border = border;
          s.clock_overlay.border_colour = Rgb{10, 20, 200};
          s.clock_overlay.h24 = h24;
          s.clock_overlay.colour = Rgb{200, 100, 50};
          CAPTURE(font.name);
          CAPTURE(static_cast<int>(corner));
          CAPTURE(border);
          Frame direct, stamped;
          artwork(direct);
          stamped.copy_from(direct);
          faces::build_overlay(*sprite, s, t);
          faces::stamp_overlay(stamped, *sprite);
          CHECK(sprite->x0 <= sprite->x1);
          CHECK(sprite->x0 >= 1);  // inside the margin
          CHECK(sprite->x1 <= 62);
          // The reference: the same text drawn straight with the font renderer, 2 px from
          // the corner's edges.
          const std::string text = p64::widgets::clock_format::time_text(t, h24, false);
          const int w = p64::gfx::fonts::width(font, text, 1), h = p64::gfx::fonts::cap_height(font, 1);
          const bool right = corner == p64::system::Corner::TopRight || corner == p64::system::Corner::BottomRight;
          const bool bottom = corner == p64::system::Corner::BottomLeft || corner == p64::system::Corner::BottomRight;
          const Rgb halo = s.clock_overlay.border_colour;
          p64::gfx::fonts::draw(direct, font, right ? 64 - w - 2 : 2, bottom ? 64 - h - 2 : 2, text, s.clock_overlay.colour, 1,
                                border ? &halo : nullptr);
          CHECK(std::memcmp(direct.data(), stamped.data(), Frame::bytes()) == 0);
        }
      }
    }
  }
}

TEST_CASE("faces: the border blends at its opacity; the text stays opaque") {
  p64::system::Settings s;
  s.clock_overlay.font = "everyday-standard";
  s.clock_overlay.colour = Rgb{255, 255, 255};
  s.clock_overlay.border_colour = Rgb{0, 0, 255};
  const tm t = at(12, 34, 0);
  auto sprite = std::make_unique<faces::OverlaySprite>();
  Frame opaque, half, faint, art;
  artwork(art);
  const std::pair<Frame *, int> runs[] = {{&opaque, 255}, {&half, 128}, {&faint, 1}};
  for (const auto &run : runs) {
    run.first->copy_from(art);
    s.clock_overlay.border_opacity = static_cast<uint8_t>(run.second);
    faces::build_overlay(*sprite, s, t);
    faces::stamp_overlay(*run.first, *sprite);
  }
  int border_px = 0, text_px = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      const uint8_t m = sprite->mask[y * 64 + x];
      if (m == p64::gfx::fonts::kMaskText) {
        ++text_px;
        CHECK(half.get(x, y) == (Rgb{255, 255, 255}));
        CHECK(faint.get(x, y) == (Rgb{255, 255, 255}));
      } else if (m == p64::gfx::fonts::kMaskOutline) {
        ++border_px;
        CHECK(opaque.get(x, y) == (Rgb{0, 0, 255}));
        Frame want;
        want.copy_from(art);
        want.blend(x, y, Rgb{0, 0, 255}, 128);
        CHECK(half.get(x, y) == want.get(x, y));
        const Rgb a = art.get(x, y), f = faint.get(x, y);
        CHECK((std::abs(f.r - a.r) <= 1 && std::abs(f.g - a.g) <= 1 && std::abs(f.b - a.b) <= 1));
      } else {
        CHECK(half.get(x, y) == art.get(x, y));
      }
    }
  CHECK(text_px > 20);
  CHECK(border_px > text_px);
}

TEST_CASE("faces: every font fits the digital clock; a date too wide falls back to the default font") {
  // High Birth's date line ("WED 23 SEP") is 79 px at 1x; it is drawn in Capital Hill.
  p64::system::Settings s;
  s.clock.h24 = false;
  const tm t = at(23, 58, 59);
  for (size_t i = 0; i < p64::gfx::fonts::kFontCount; ++i) {
    s.clock.font = p64::gfx::fonts::kFonts[i]->name;
    CAPTURE(s.clock.font);
    for (int scale = 1; scale <= 3; ++scale) {
      s.clock.scale = static_cast<uint8_t>(scale);
      Frame f;
      faces::draw_clock(f, s, &t);
      CHECK(lit_rect(f, 0, 0, 63, 63) > 40);
      CHECK_EQ(lit_rect(f, 0, 0, 0, 63), 0);
      CHECK_EQ(lit_rect(f, 63, 0, 63, 63), 0);
      CHECK_EQ(lit_rect(f, 0, 63, 63, 63), 0);
    }
  }
}

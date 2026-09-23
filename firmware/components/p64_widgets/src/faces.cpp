// The widgets' faces, drawn from the settings, the time and the data (no ESP-IDF include;
// host-tested in tests/host/unit/widgets.cpp; review of 2026-09-22, P-T2). widgets.cpp
// fetches the data and the time and asks these for the pixels.
#include "faces.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "analogue.hpp"
#include "clock_format.hpp"
#include "p64/gfx/fonts.hpp"
#include "weather_icons.hpp"

namespace p64::widgets::faces {

using gfx::Frame;
using gfx::Rgb;

const gfx::fonts::Font &font_named(const std::string &name) {
  const gfx::fonts::Font *f = gfx::fonts::by_name(name);
  return f ? *f : gfx::fonts::default_font();
}

const gfx::fonts::Font &overlay_font(const std::string &name) {
  const gfx::fonts::Font *f = gfx::fonts::by_name(name);
  return f && f->overlay ? *f : gfx::fonts::default_font();
}

namespace {

// FNV-1a, for the overlay key.
uint32_t mix(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xff;
    h *= 16777619u;
  }
  return h;
}

}  // namespace

uint32_t overlay_key(const system::Settings &s, const tm &t) {
  uint32_t h = 2166136261u;
  h = mix(h, static_cast<uint32_t>(t.tm_hour * 60 + t.tm_min));
  h = mix(h, static_cast<uint32_t>(s.clock_overlay.corner));
  const system::Settings::ClockOverlay &o = s.clock_overlay;
  h = mix(h, (o.h24 ? 1u : 0u) | (o.border ? 2u : 0u) | (static_cast<uint32_t>(o.border_opacity) << 8));
  h = mix(h, static_cast<uint32_t>(o.colour.r | (o.colour.g << 8) | (o.colour.b << 16)));
  h = mix(h, static_cast<uint32_t>(o.border_colour.r | (o.border_colour.g << 8) | (o.border_colour.b << 16)));
  for (const char *c = overlay_font(s.clock_overlay.font).name; *c; ++c) h = mix(h, static_cast<unsigned char>(*c));
  return h ? h : 1;
}

namespace {

// Where the overlay's text goes: its font, text and top-left corner.
struct OverlayLayout {
  const gfx::fonts::Font *font;
  std::string text;
  int x, y;
};

OverlayLayout overlay_layout(const system::Settings &s, const tm &t) {
  OverlayLayout l{&overlay_font(s.clock_overlay.font), clock_format::time_text(t, s.clock_overlay.h24, false), 0, 0};
  const int w = gfx::fonts::width(*l.font, l.text, 1);
  const int h = gfx::fonts::cap_height(*l.font, 1);
  const int margin = 2;  // one pixel plus the outline
  l.x = l.y = margin;
  const system::Corner c = s.clock_overlay.corner;
  if (c == system::Corner::TopRight || c == system::Corner::BottomRight) l.x = Frame::width() - w - margin;
  if (c == system::Corner::TopCenter || c == system::Corner::BottomCenter) l.x = (Frame::width() - w) / 2;
  if (c == system::Corner::BottomLeft || c == system::Corner::BottomRight || c == system::Corner::BottomCenter) l.y = Frame::height() - h - margin;
  return l;
}

}  // namespace

void build_overlay(OverlaySprite &out, const system::Settings &s, const tm &t) {
  const OverlayLayout l = overlay_layout(s, t);
  std::memset(out.mask, 0, sizeof(out.mask));
  gfx::fonts::draw_mask(out.mask, *l.font, l.x, l.y, l.text, 1, s.clock_overlay.border);
  out.text = s.clock_overlay.colour;
  out.border = s.clock_overlay.border_colour;
  out.border_opacity = s.clock_overlay.border_opacity;
  out.x0 = Frame::width();
  out.y0 = Frame::height();
  out.x1 = out.y1 = -1;
  for (int y = 0; y < Frame::height(); ++y) {
    for (int x = 0; x < Frame::width(); ++x) {
      if (!out.mask[y * Frame::width() + x]) continue;
      out.x0 = std::min(out.x0, x);
      out.x1 = std::max(out.x1, x);
      out.y0 = std::min(out.y0, y);
      out.y1 = std::max(out.y1, y);
    }
  }
}

void stamp_overlay(Frame &frame, const OverlaySprite &sprite) {
  uint8_t *px = frame.pixels();
  const unsigned a = sprite.border_opacity, ia = 255u - a;
  const Rgb b = sprite.border;
  for (int y = sprite.y0; y <= sprite.y1; ++y) {
    const uint8_t *m = &sprite.mask[y * Frame::width()];
    for (int x = sprite.x0; x <= sprite.x1; ++x) {
      if (!m[x]) continue;
      uint8_t *p = &px[(y * Frame::width() + x) * 3];
      if (m[x] == gfx::fonts::kMaskText || a == 255) {
        const Rgb c = m[x] == gfx::fonts::kMaskText ? sprite.text : b;
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
      } else {  // the same rounding as Frame::blend()
        p[0] = static_cast<uint8_t>((p[0] * ia + b.r * a + 127u) / 255u);
        p[1] = static_cast<uint8_t>((p[1] * ia + b.g * a + 127u) / 255u);
        p[2] = static_cast<uint8_t>((p[2] * ia + b.b * a + 127u) / 255u);
      }
    }
  }
}

std::string temperature_text(float value, bool decimals) {
  char buf[16];
  if (decimals) {
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(value));
  } else {
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
  }
  return buf;
}

void draw_trend(Frame &f, int x, int y, float per_hour, Rgb colour) {
  // Up, down or flat: a small arrow in a 5x5 box.
  if (per_hour > 0.3f) {
    for (int i = 0; i < 3; ++i) f.fill_rect(x + 2 - i, y + i, 1 + 2 * i, 1, colour);
    f.fill_rect(x + 2, y + 3, 1, 2, colour);
  } else if (per_hour < -0.3f) {
    f.fill_rect(x + 2, y, 1, 2, colour);
    for (int i = 0; i < 3; ++i) f.fill_rect(x + i, y + 2 + i, 5 - 2 * i, 1, colour);
  } else {
    f.fill_rect(x, y + 2, 5, 1, colour);
    f.fill_rect(x + 3, y + 1, 1, 3, colour);
  }
}

uint32_t draw_clock(Frame &out, const system::Settings &s, const tm *time) {
  const gfx::fonts::Font &font = font_named(s.clock.font);
  out.clear(s.clock.background);
  if (!time) {
    gfx::fonts::draw_centred(out, font, 20, "--:--", s.clock.colour, 2);
    gfx::fonts::draw_centred(out, font, 42, "NO TIME", s.clock.colour, 1);
    return 1000;
  }
  const tm &t = *time;
  if (s.clock.analogue) {
    analogue::Style st;
    st.ink = s.clock.colour;
    st.background = s.clock.background;
    st.seconds = s.clock.seconds;
    st.month_first = s.clock.month_first;
    // The numerals sit inside the rim ticks: a font taller than 7 px would cover them.
    st.font = font.size <= 7 ? &font : &gfx::fonts::default_font();
    analogue::draw(out, st, t);
    const uint32_t delay_ms = s.clock.seconds ? 1000 : static_cast<uint32_t>((60 - t.tm_sec) * 1000);
    return delay_ms > 60000 ? 60000 : delay_ms;
  }
  const bool colon = !s.clock.blink_colon || (t.tm_sec % 2 == 0);
  std::string text = clock_format::time_text(t, s.clock.h24, s.clock.seconds, colon);
  const std::string mer = clock_format::meridiem(t, s.clock.h24);
  int scale = s.clock.scale;
  while (scale > 1 && gfx::fonts::width(font, text, scale) + (mer.empty() ? 0 : gfx::fonts::width(font, mer, 1) + 2) > Frame::width() - 2) --scale;
  const int th = gfx::fonts::cap_height(font, scale);
  const int total = gfx::fonts::width(font, text, scale) + (mer.empty() ? 0 : gfx::fonts::width(font, mer, 1) + 2);
  const int x = (Frame::width() - total) / 2;
  const int y = 14;
  gfx::fonts::draw(out, font, x, y, text, s.clock.colour, scale);
  if (!mer.empty()) gfx::fonts::draw(out, font, x + gfx::fonts::width(font, text, scale) + 2, y + th - gfx::fonts::cap_height(font, 1), mer, s.clock.colour, 1);
  // The date in the same font when it fits the panel, else in the default one.
  const std::string date = clock_format::date_text(t, s.clock.month_first);
  const gfx::fonts::Font &date_font = gfx::fonts::width(font, date, 1) <= Frame::width() - 2 ? font : gfx::fonts::default_font();
  gfx::fonts::draw_centred(out, date_font, y + th + 8, date, s.clock.colour, 1);
  // Until the next second when seconds or the blink show, else until the next minute.
  const uint32_t delay_ms = (s.clock.seconds || s.clock.blink_colon) ? 1000 : static_cast<uint32_t>((60 - t.tm_sec) * 1000);
  return delay_ms > 60000 ? 60000 : delay_ms;
}

void draw_weather(Frame &out, const system::Settings &s, const weather_model::Forecast &f, const std::string &error,
                  int64_t now) {
  const gfx::fonts::Font &font = gfx::fonts::default_font();
  const Rgb ink = s.clock.colour;
  const Rgb dim{140, 140, 160};
  out.clear(s.clock.background);
  if (!s.weather.location_set) {
    gfx::fonts::draw_centred(out, font, 20, "WEATHER", ink, 1);
    gfx::fonts::draw_centred(out, font, 32, "SET A", dim, 1);
    gfx::fonts::draw_centred(out, font, 40, "LOCATION", dim, 1);
    return;
  }
  if (!f.valid || now - f.fetched_us > kWeatherStaleUs) {
    gfx::fonts::draw_centred(out, font, 20, "WEATHER", ink, 1);
    gfx::fonts::draw_centred(out, font, 32, "NO DATA", dim, 1);
    if (!error.empty()) gfx::fonts::draw_centred(out, font, 44, error.substr(0, 10), dim, 1);
    return;
  }
  const icons::Group group = icons::group_for_code(f.code);
  if (const icons::Icon *ic = icons::find(group, !f.is_day, 24)) icons::draw(out, *ic, 2, 3);
  const std::string temp = temperature_text(f.temperature, false);
  gfx::fonts::draw(out, font, 30, 3, temp, ink, 2);
  gfx::fonts::draw(out, font, 30 + gfx::fonts::width(font, temp, 2) + 2, 3, f.imperial ? "F" : "C", ink, 1);
  gfx::fonts::draw(out, font, 30, 18, "H " + temperature_text(f.today.max, false), dim, 1);
  gfx::fonts::draw(out, font, 30, 25, "L " + temperature_text(f.today.min, false), dim, 1);
  // The next days: a 21-pixel column each.
  for (int i = 0; i < f.day_count && i < 3; ++i) {
    const weather_model::Day &d = f.days[i];
    const int x0 = i * 21 + (i == 2 ? 1 : 0);
    const char *wd = clock_format::weekday_short(d.weekday);
    const std::string letter = wd[0] ? std::string(1, wd[0]) : "?";
    gfx::fonts::draw(out, font, x0 + 8, 31, letter, ink, 1);
    if (const icons::Icon *ic = icons::find(icons::group_for_code(d.code), false, 12)) icons::draw(out, *ic, x0 + 4, 37);
    const std::string hi = temperature_text(d.max, false), lo = temperature_text(d.min, false);
    gfx::fonts::draw(out, font, x0 + (21 - gfx::fonts::width(font, hi, 1)) / 2, 50, hi, ink, 1);
    gfx::fonts::draw(out, font, x0 + (21 - gfx::fonts::width(font, lo, 1)) / 2, 57, lo, dim, 1);
  }
  // The age of the data, small, when it is old.
  const int64_t age_min = (now - f.fetched_us) / (60 * kSecond);
  if (age_min >= 90) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lldh", static_cast<long long>(age_min / 60));
    gfx::fonts::draw(out, font, 64 - gfx::fonts::width(font, buf, 1) - 1, 26, buf, dim, 1);
  }
  return;
}

void draw_temperature(Frame &out, const system::Settings &s, const Reading &r) {
  const gfx::fonts::Font &font = gfx::fonts::default_font();
  const Rgb ink = s.clock.colour;
  const Rgb dim{140, 140, 160};
  out.clear(s.clock.background);
  if (!r.valid) {
    gfx::fonts::draw_centred(out, font, 22, "NO", ink, 2);
    gfx::fonts::draw_centred(out, font, 40, "SENSOR", dim, 1);
    return;
  }
  const bool imperial = s.weather.imperial;
  const float value = imperial ? r.temperature_c * 9.0f / 5.0f + 32.0f : r.temperature_c;
  const std::string temp = temperature_text(value, true);
  const int w = gfx::fonts::width(font, temp, 2) + 2 + gfx::fonts::width(font, "C", 1);
  const int x = (Frame::width() - w) / 2;
  gfx::fonts::draw(out, font, x, 12, temp, ink, 2);
  gfx::fonts::draw(out, font, x + gfx::fonts::width(font, temp, 2) + 2, 12, imperial ? "F" : "C", ink, 1);
  char hum[16];
  std::snprintf(hum, sizeof(hum), "%d%% RH", static_cast<int>(std::lround(r.humidity)));
  gfx::fonts::draw_centred(out, font, 34, hum, dim, 1);
  if (s.temperature.trend) {
    const float trend = imperial ? r.trend_c_per_hour * 9.0f / 5.0f : r.trend_c_per_hour;
    draw_trend(out, 29, 46, trend, ink);
    char tb[16];
    std::snprintf(tb, sizeof(tb), "%+.1f/H", static_cast<double>(trend));
    gfx::fonts::draw_centred(out, font, 54, tb, dim, 1);
  }
  return;
}

}  // namespace p64::widgets::faces

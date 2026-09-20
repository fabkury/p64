#include "analogue.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

#include "clock_format.hpp"

namespace p64::widgets::analogue {
namespace {

constexpr float kPi = 3.14159265358979f;

gfx::Rgb dim(gfx::Rgb c, int percent) {
  return gfx::Rgb{static_cast<uint8_t>(c.r * percent / 100), static_cast<uint8_t>(c.g * percent / 100),
                  static_cast<uint8_t>(c.b * percent / 100)};
}

void polar(float angle_deg, float radius, int &x, int &y) {
  const float a = angle_deg * kPi / 180.0f;
  // The centre sits between pixels (31.5); a nudge keeps the axis-aligned hands on the
  // same column and row whichever side sin() and cos() land on at exact right angles.
  x = static_cast<int>(std::lround(kCentre + radius * std::sin(a) + 1e-4f));
  y = static_cast<int>(std::lround(kCentre - radius * std::cos(a) + 1e-4f));
}

// Bresenham, no anti-aliasing: the pixel-art look.
void line(gfx::Frame &f, int x0, int y0, int x1, int y1, gfx::Rgb c) {
  const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    f.set(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// A 2-pixel-wide line: the line plus a copy shifted one pixel across its dominant axis.
void thick_line(gfx::Frame &f, int x0, int y0, int x1, int y1, gfx::Rgb c) {
  line(f, x0, y0, x1, y1, c);
  if (std::abs(x1 - x0) >= std::abs(y1 - y0)) {
    line(f, x0, y0 + 1, x1, y1 + 1, c);  // mostly horizontal: thicken downwards
  } else {
    line(f, x0 + 1, y0, x1 + 1, y1, c);  // mostly vertical: thicken rightwards
  }
}

void hand(gfx::Frame &f, float angle_deg, int length, gfx::Rgb c, bool thick) {
  int x, y;
  polar(angle_deg, static_cast<float>(length), x, y);
  const int cx = static_cast<int>(kCentre), cy = static_cast<int>(kCentre);  // 31: the hub's top-left pixel
  if (thick) {
    thick_line(f, cx, cy, x, y, c);
  } else {
    line(f, cx, cy, x, y, c);
  }
}

}  // namespace

void hand_end(float angle_deg, int length, int &x, int &y) { polar(angle_deg, static_cast<float>(length), x, y); }

void draw(gfx::Frame &frame, const Style &style, const tm &t) {
  const gfx::fonts::Font &font = style.font ? *style.font : gfx::fonts::default_font();
  frame.clear(style.background);
  const gfx::Rgb tick_dim = dim(style.ink, 55);
  // Ticks: every hour at the rim; the cardinal ones longer and in the full ink.
  for (int h = 0; h < 12; ++h) {
    const bool cardinal = h % 3 == 0;
    const float a = h * 30.0f;
    int x0, y0, x1, y1;
    polar(a, static_cast<float>(kRimRadius), x1, y1);
    polar(a, static_cast<float>(kRimRadius - (cardinal ? 3 : 1)), x0, y0);
    line(frame, x0, y0, x1, y1, cardinal ? style.ink : tick_dim);
  }
  // Numerals inside the cardinal ticks.
  const int cap = gfx::fonts::cap_height(font, 1);
  gfx::fonts::draw_centred(frame, font, 5, "12", style.ink, 1);
  gfx::fonts::draw_centred(frame, font, 64 - 5 - cap, "6", style.ink, 1);
  gfx::fonts::draw(frame, font, 64 - 5 - gfx::fonts::width(font, "3", 1), 32 - cap / 2, "3", style.ink, 1);
  gfx::fonts::draw(frame, font, 5, 32 - cap / 2, "9", style.ink, 1);
  // The date, dimmed, under the centre; the hands are drawn over it.
  gfx::fonts::draw_centred(frame, font, 41, clock_format::date_text(t, style.month_first).substr(4), dim(style.ink, 55), 1);
  // Hands: the hour hand moves with the minutes; the minute hand steps per minute.
  const float minute_angle = t.tm_min * 6.0f;
  const float hour_angle = (t.tm_hour % 12) * 30.0f + t.tm_min * 0.5f;
  hand(frame, hour_angle, kHourHand, style.ink, true);
  hand(frame, minute_angle, kMinuteHand, style.ink, false);
  if (style.seconds) hand(frame, t.tm_sec * 6.0f, kSecondHand, style.accent, false);
  // The hub: a 2x2 block on the centre.
  frame.fill_rect(static_cast<int>(kCentre), static_cast<int>(kCentre), 2, 2, style.ink);
}

}  // namespace p64::widgets::analogue

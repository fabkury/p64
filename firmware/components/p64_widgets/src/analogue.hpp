// The analogue clock face (spec 7.1), drawn for the 64x64 panel and settled with the
// user on 2026-09-20: twelve tick marks at the rim (the four cardinal ones longer), the
// numerals 12, 3, 6 and 9 in the bundled 6 px font, crisp pixel hands (hour hand 2 px
// wide and short, moving continuously; minute hand 1 px and long, stepping per minute;
// an optional 1 px second hand in the accent colour), a hub, and the date in small
// dimmed text under the centre. Pure (host-tested): it only draws into a Frame.
#pragma once

#include <ctime>

#include "p64/gfx/fonts.hpp"
#include "p64/gfx/frame.hpp"

namespace p64::widgets::analogue {

struct Style {
  gfx::Rgb ink{255, 255, 255};       // ticks, numerals, hour and minute hands, hub
  gfx::Rgb accent{255, 77, 151};     // the second hand
  gfx::Rgb background{0, 0, 0};
  bool seconds = false;              // draw the second hand
  bool month_first = false;          // the date's order
  const gfx::fonts::Font *font = nullptr;  // numerals and the date (default font when null)
};

// Geometry, exposed for the tests: the dial's centre and radii in pixels.
constexpr float kCentre = 31.5f;   // the panel's middle (pixel centres at n + 0.5)
constexpr int kRimRadius = 31;     // outermost tick pixel
constexpr int kHourHand = 14;
constexpr int kMinuteHand = 23;
constexpr int kSecondHand = 26;

// Clears the frame to the background and draws the face for `t`.
void draw(gfx::Frame &frame, const Style &style, const tm &t);

// The pixel at the end of a hand of `length` pointing at `angle_deg` (0 = 12 o'clock,
// clockwise), rounded like the drawing does.
void hand_end(float angle_deg, int length, int &x, int &y);

}  // namespace p64::widgets::analogue

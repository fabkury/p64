// The themed clock faces (spec 7.1, approved 2026-09-26): flip, nixie, horizon, words,
// hourglass, orrery and, since 2026-09-28, the LED, each drawn from pixel-art assets (clock_assets, baked from
// assets/clock/) onto a Frame. Pure and host-tested: tools/mock_clock_faces.py draws the
// same faces on the host with the same arithmetic, and tests/host/run.py compares the
// firmware's pixels with its references (tests/host/corpus/clock/).
#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::widgets::themed {

// The local time a face draws.
struct Moment {
  int hour = 0, minute = 0, second = 0;
  int wday = 6;             // 0 = Sunday
  int mday = 26, mon = 9;   // month 1..12
  int yday = 268;           // 0 = 1 January
  int year = 2026;
  static Moment from(const tm &t);
  int h12() const { return hour % 12 ? hour % 12 : 12; }
  const char *meridiem() const { return hour < 12 ? "AM" : "PM"; }
};

// The clock settings a themed face honours (colours and fonts are the face's own).
struct Options {
  bool seconds = false;      // the per-second element: the flip's rail, the hourglass's stream, Mercury
  bool blink = false;        // the colon is off on odd seconds (nixie, horizon, orrery)
  bool h24 = true;           // else 12 h with AM or PM
  bool month_first = false;  // the date's order
};

const char *weekday_name(int wday);  // "SUN".."SAT"
const char *month_name(int mon);     // "JAN".."DEC"
// "SAT 26 SEP" or "SAT SEP 26".
std::string date_text(const Moment &m, bool month_first);
// The hour as the faces show it: "07" in 24 h, " 7" in 12 h (the blank draws nothing).
std::string hour_text(const Moment &m, const Options &o);
bool colon_on(const Moment &m, const Options &o);

// 1. Flip: two split-flap tiles. `phase` 0 draws the face at rest; 1..kFlipFrames the
// frames of the change from `from` to `m` (the old upper leaf falls, the new lower leaf
// lands; the hour tile one frame behind the minute tile), each held kFlipFrameMs.
constexpr int kFlipFrames = 10;
constexpr uint32_t kFlipFrameMs = 45;
void draw_flip(gfx::Frame &frame, const Moment &m, const Options &o, int phase = 0, const Moment *from = nullptr);

// 2. Nixie: four tubes on a base.
void draw_nixie(gfx::Frame &frame, const Moment &m, const Options &o);

// 3. Horizon: a landscape from the sun's real position.
struct Sky {
  float latitude = 40, longitude = 0, tz_hours = 0;  // without a location: 40 N, local time as solar time
  uint8_t cover = 0;                                 // 0 clear, 1 few, 2 broken, 3 overcast
  enum class Precip : uint8_t { None, Rain, Snow } precip = Precip::None;
};
void draw_horizon(gfx::Frame &frame, const Moment &m, const Options &o, const Sky &sky);
// The row of a body's centre at column x (the sun's disc has radius 3, the moon's 4): on
// the far hills' line at +0.7 degrees, behind it by -0.83 (then -1), a round arc above.
int horizon_body_row(double elevation, int x, int radius);

// 4. Words: the time spelled on a letter grid (twelve-hour by nature).
void draw_words(gfx::Frame &frame, const Moment &m, const Options &o);
// The words lit at a time ("HALF", "PAST", "TEN"; "FIVEm"/"TENm" are the minute words),
// and the corner dots (the minutes past the five).
bool word_lit(const char *word, int hour, int minute);
int word_dots(int minute);

// 5. Hourglass: the hour as sand.
void draw_hourglass(gfx::Frame &frame, const Moment &m, const Options &o);

// 6. Orrery: the Earth, the Moon and Mercury as the hands.
void draw_orrery(gfx::Frame &frame, const Moment &m, const Options &o);
// The bodies' pixel centres (for the tests).
void orrery_positions(const Moment &m, int &ex, int &ey, int &mx, int &my, int &qx, int &qy);

// 7. LED: a seven-segment clock in five styles (the same order as system::LedStyle).
// `millis` is the milliseconds into the second (the face's clock is the time of day in
// milliseconds: the glow's pulse and the VFD's meter follow it, a frame every
// kLedPulseStepMs); `phase` 1..kLedFadeFrames draws that frame of the cross-fade from
// `from` (every digit that differs), each held kLedFadeMs.
enum class LedStyle : uint8_t { Red = 0, Green = 1, Amber = 2, Blue = 3, Vfd = 4 };
constexpr int kLedFadeFrames = 5;
constexpr uint32_t kLedFadeMs = 40, kLedPulseStepMs = 200;
void draw_led(gfx::Frame &frame, const Moment &m, const Options &o, LedStyle style, int millis = 0, int phase = 0,
              const Moment *from = nullptr);
// The face's clock and the glow's pulse (255 at 0, 140 two seconds later, a triangle).
int led_ms(const Moment &m, int millis);
int led_pulse(int ms);

}  // namespace p64::widgets::themed

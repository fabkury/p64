// The LED face (spec 7.1, prompts p052 and p053, 2026-09-28): a seven-segment clock in
// Digital Display's 15x19 digits (assets/clock/led: the digits, the colon, the 5x9 mini
// digits, the plastic bezel and the VFD's chrome frame), hours upper left and minutes
// lower right, the seconds lower left, the AM, PM and ALM indicators upper right, a ghost
// "8" behind every digit. Five styles: red, green, amber and blue LEDs behind a tinted
// filter with the labels printed on it and a lit dot beside each, and a cyan VFD on dark
// glass whose indicator words light up, with a bell and a six-bar meter. Everything lit
// goes first into an intensity map and a ghost map, then the window is coloured: a lit
// pixel between the ghost and the lit colour by its intensity, a one-pixel glow around
// the lit pixels (the brightest of the eight neighbours, scaled by a four-second pulse).
// A digit change cross-fades over kLedFadeFrames frames. The maps live in the Frame
// itself while it is built (R = intensity, G = ghost) and the window is composed in place
// behind a three-row window of the original intensities, so no scratch buffer is needed.
// The face's clock is the local time of day in milliseconds, so every frame is a function
// of the moment: tools/mock_clock_faces.py draws the same pixels and the references.
#include <algorithm>
#include <string>

#include "clock_assets.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr int kDigitW = 15, kMiniW = 5;
constexpr int kWin0 = 3, kWin1 = 61;  // the window inside the 3 px frame, [kWin0, kWin1)
constexpr int kHoursX = 6, kHoursY = 7, kMinutesX = 27, kMinutesY = 38;
constexpr int kColonX = 38, kColonY = 7, kSecondsX = 7, kSecondsY = 48;
constexpr int kRows[3] = {8, 15, 22}, kLabelX = 47, kDotX = 43;
constexpr int kVuX = 6, kVuY = 35, kVuBars = 6, kVuH = 6;
constexpr int kPulseMs = 4000;
constexpr const char *kWords[3] = {"AM", "PM", "ALM"};
// The bell beside ALM on the VFD, 5x5.
constexpr uint8_t kBell[5] = {0b00100, 0b01110, 0b01110, 0b11111, 0b00100};

struct Style {
  Rgb window, window_alt;  // the filter or the glass; the glass's odd rows (the mesh)
  Rgb ghost, lit, glow;
  Rgb label;  // the printed labels (LEDs)
  bool vfd;
};
// The ghost and the glow are a third of the first design (p054, 2026-09-28): the matrix
// lifts the dark end and the unlit segments competed with the lit ones on the panel.
constexpr Style kStyles[5] = {
    {{10, 2, 2}, {10, 2, 2}, {16, 3, 2}, {255, 48, 24}, {27, 5, 2}, {128, 78, 72}, false},
    {{2, 8, 3}, {2, 8, 3}, {3, 14, 4}, {60, 255, 70}, {4, 27, 6}, {76, 120, 84}, false},
    {{10, 6, 1}, {10, 6, 1}, {16, 10, 2}, {255, 160, 24}, {28, 18, 2}, {128, 104, 70}, false},
    {{2, 3, 12}, {2, 3, 12}, {3, 5, 18}, {60, 120, 255}, {4, 9, 32}, {80, 90, 130}, false},
    {{5, 11, 11}, {3, 8, 8}, {5, 15, 13}, {150, 255, 225}, {7, 22, 20}, {0, 0, 0}, true},
};

// a towards b by v/255.
Rgb mix(Rgb a, Rgb b, int v) {
  const auto ch = [v](uint8_t p, uint8_t q) { return static_cast<uint8_t>(p + ((q - p) * v) / 255); };
  return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

// The maps: R holds the lit intensity, G the ghost flag.
void lit_at(Frame &f, int x, int y, int value) {
  Rgb c = f.get(x, y);
  if (value > c.r) c.r = static_cast<uint8_t>(value);
  f.set(x, y, c);
}
void ghost_at(Frame &f, int x, int y) {
  Rgb c = f.get(x, y);
  c.g = 1;
  f.set(x, y, c);
}
void stamp_lit(Frame &f, const sprite::View &v, int x, int y, int value) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) lit_at(f, x + xx, y + yy, value);
}
void stamp_ghost(Frame &f, const sprite::View &v, int x, int y) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) ghost_at(f, x + xx, y + yy);
}

// A digit slot of `sheet` (ten cells, the 8 is the ghost): `now` lit; during a change
// (`t` 1..254) the pixels only in `old` at 255 - t, the ones only in `now` at t, the
// shared ones at 255. A space draws nothing (the 12 h leading blank).
void digit(Frame &f, const assets::Sprite &sheet, int x, int y, char now, char old, int t) {
  const sprite::View eight = sprite::cell(sheet, 10, 8);
  stamp_ghost(f, eight, x, y);
  if (now == old || t <= 0 || t >= 255) {
    if (now != ' ') stamp_lit(f, sprite::cell(sheet, 10, now - '0'), x, y, 255);
    return;
  }
  const sprite::View n = sprite::cell(sheet, 10, now == ' ' ? 8 : now - '0');
  const sprite::View o = sprite::cell(sheet, 10, old == ' ' ? 8 : old - '0');
  for (int yy = 0; yy < eight.h; ++yy)
    for (int xx = 0; xx < eight.w; ++xx) {
      const bool a = old != ' ' && o.inked(xx, yy), b = now != ' ' && n.inked(xx, yy);
      if (a || b) lit_at(f, x + xx, y + yy, a && b ? 255 : (b ? t : 255 - t));
    }
}

sprite::View letter(char ch) { return sprite::cell(assets::kWordsAlphabet, 26, ch - 'A'); }

void word(Frame &f, int x, int y, const char *text, bool on) {
  for (; *text; ++text, x += 4) {
    stamp_ghost(f, letter(*text), x, y);
    if (on) stamp_lit(f, letter(*text), x, y, 255);
  }
}

int tri255(int ms, int period) {
  const int ph = ms % period;
  return (ph < period / 2 ? 2 * ph : 2 * (period - ph)) * 255 / period;
}

// The meter's bar i, 1..6 rows: two triangles of unrelated periods.
int vu_height(int ms, int i) { return 1 + ((kVuH - 1) * (tri255(ms, 900 + 170 * i) + tri255(ms, 1300 + 230 * i))) / 510; }

// The window coloured from the maps, in place: `above` and `here` keep the original
// intensities of the two rows the glow reads that the composition has already overwritten;
// the row below is still original in the frame.
void compose(Frame &f, const Style &st, int pulse) {
  uint8_t above[Frame::width()] = {}, here[Frame::width()];
  for (int y = kWin0; y < kWin1; ++y) {
    for (int x = 0; x < Frame::width(); ++x) here[x] = f.get(x, y).r;
    for (int x = kWin0; x < kWin1; ++x) {
      const Rgb map = f.get(x, y);
      const Rgb base = map.g ? st.ghost : (y % 2 == 0 ? st.window : st.window_alt);
      if (here[x]) {
        f.set(x, y, mix(base, st.lit, here[x]));
        continue;
      }
      int n = 0;
      for (int dx = -1; dx <= 1; ++dx) {
        const int xx = x + dx;
        n = std::max({n, static_cast<int>(above[xx]), static_cast<int>(here[xx]), static_cast<int>(f.get(xx, y + 1).r)});
      }
      f.set(x, y, n ? mix(base, st.glow, (n * pulse) / 255) : base);
    }
    for (int x = 0; x < Frame::width(); ++x) above[x] = here[x];
  }
}

}  // namespace

int led_ms(const Moment &m, int millis) { return ((m.hour * 60 + m.minute) * 60 + m.second) * 1000 + millis; }

int led_pulse(int ms) {
  const int ph = ms % kPulseMs;
  const int tri = ph < kPulseMs / 2 ? ph : kPulseMs - ph;
  return 255 - (115 * tri) / (kPulseMs / 2);
}

void draw_led(Frame &frame, const Moment &m, const Options &o, LedStyle style, int millis, int phase, const Moment *from) {
  const Style &st = kStyles[std::min<int>(static_cast<int>(style), 4)];
  frame.clear(gfx::kBlack);
  const int t = from ? (255 * phase) / kLedFadeFrames : 0;
  const Moment &prev = from ? *from : m;
  const std::string hh = hour_text(m, o), ph = hour_text(prev, o);
  const std::string mm = (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  const std::string pm = (prev.minute < 10 ? "0" : "") + std::to_string(prev.minute);
  for (int i = 0; i < 2; ++i) {
    digit(frame, assets::kLedDigits, kHoursX + i * (kDigitW + 1), kHoursY, hh[i], ph[i], t);
    digit(frame, assets::kLedDigits, kMinutesX + i * (kDigitW + 1), kMinutesY, mm[i], pm[i], t);
  }
  const sprite::View colon = sprite::view(assets::kLedColon);
  stamp_ghost(frame, colon, kColonX, kColonY);
  if (colon_on(m, o)) stamp_lit(frame, colon, kColonX, kColonY, 255);
  if (o.seconds) {
    const std::string ss = (m.second < 10 ? "0" : "") + std::to_string(m.second);
    const std::string ps = (prev.second < 10 ? "0" : "") + std::to_string(prev.second);
    for (int i = 0; i < 2; ++i) digit(frame, assets::kLedMini, kSecondsX + i * (kMiniW + 1), kSecondsY, ss[i], ps[i], t);
  }
  const bool on[3] = {!o.h24 && m.hour < 12, !o.h24 && m.hour >= 12, true};
  const int ms = led_ms(m, millis);
  if (st.vfd) {
    for (int r = 0; r < 3; ++r) word(frame, kLabelX, kRows[r], kWords[r], on[r]);
    for (int yy = 0; yy < 5; ++yy)
      for (int xx = 0; xx < 5; ++xx)
        if (kBell[yy] & (1 << (4 - xx))) {
          ghost_at(frame, kDotX - 2 + xx, kRows[2] + yy);
          lit_at(frame, kDotX - 2 + xx, kRows[2] + yy, 255);
        }
    for (int i = 0; i < kVuBars; ++i) {
      const int h = vu_height(ms, i);
      for (int r = 0; r < kVuH; ++r)
        for (int xx = 0; xx < 2; ++xx) {
          ghost_at(frame, kVuX + 3 * i + xx, kVuY - r);
          if (r < h) lit_at(frame, kVuX + 3 * i + xx, kVuY - r, 255);
        }
    }
  } else {
    for (int r = 0; r < 3; ++r)
      for (int yy = 0; yy < 2; ++yy)
        for (int xx = 0; xx < 2; ++xx) {
          ghost_at(frame, kDotX + xx, kRows[r] + 1 + yy);
          if (on[r]) lit_at(frame, kDotX + xx, kRows[r] + 1 + yy, 255);
        }
  }
  compose(frame, st, led_pulse(ms));
  sprite::blit(frame, sprite::view(st.vfd ? assets::kLedVfdFrame : assets::kLedBezel), 0, 0);
  if (!st.vfd)
    for (int r = 0; r < 3; ++r)
      for (int i = 0; kWords[r][i]; ++i) sprite::stamp(frame, letter(kWords[r][i]), kLabelX + 4 * i, kRows[r], st.label);
}

}  // namespace p64::widgets::themed

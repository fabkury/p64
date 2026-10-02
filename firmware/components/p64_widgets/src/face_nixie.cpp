// The nixie face: four tubes (assets/clock/nixie: the lit tube with its haze, ten 7x11
// wire numerals, the brass-and-walnut base), the numerals in orange with a one-pixel
// halo, the reflection down the glass screened over them, a neon colon, the date in dim
// amber under the base.
#include "clock_assets.hpp"
#include "p64/gfx/fonts.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kHalo{150, 46, 6}, kInk{255, 132, 30}, kDim{90, 30, 4}, kDate{150, 96, 36};
constexpr int kTubeX[4] = {2, 17, 34, 49};
constexpr int kTubeY = 8;
// The reflection down the left of the glass (column 2, rows 6..22 of a tube), drawn after
// the numerals and screened over them: it lies on the glass, in front of the wires (p066,
// 2026-10-02; it was in the tube asset and the halo covered it). Screened over the haze
// it is the tube's old highlight colour, {96, 60, 40}; over a lit numeral it pales it.
constexpr Rgb kShine{74, 52, 38};
constexpr int kShineX = 2, kShineY0 = 6, kShineY1 = 22;

// The screen blend: lighter than either, never darker.
Rgb screen(Rgb a, Rgb b) {
  const auto ch = [](int p, int q) { return static_cast<uint8_t>(p + q - p * q / 255); };
  return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

}  // namespace

void draw_nixie(Frame &frame, const Moment &m, const Options &o) {
  frame.clear(gfx::kBlack);
  const gfx::fonts::Font &small = gfx::fonts::default_font();
  for (int x : kTubeX) sprite::blit(frame, sprite::view(assets::kNixieTube), x, kTubeY);
  const std::string text = hour_text(m, o) + (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  for (int i = 0; i < 4; ++i) {
    if (text[i] == ' ') continue;
    const sprite::View digit = sprite::cell(assets::kNixieDigits, 10, text[i] - '0');
    sprite::stamp_halo(frame, digit, kTubeX[i] + 3, kTubeY + 10, kHalo);
    sprite::stamp(frame, digit, kTubeX[i] + 3, kTubeY + 10, kInk);
  }
  for (int x : kTubeX) {
    for (int y = kTubeY + kShineY0; y <= kTubeY + kShineY1; ++y) {
      frame.set(x + kShineX, y, screen(frame.get(x + kShineX, y), kShine));
    }
  }
  if (colon_on(m, o)) {
    for (int y : {kTubeY + 11, kTubeY + 18}) {
      for (int x : {31, 32}) {
        frame.set(x, y, kDim);
        frame.set(x, y + 1, kInk);
        frame.set(x, y + 2, kDim);
      }
    }
  }
  sprite::blit(frame, sprite::view(assets::kNixieBase), 0, kTubeY + 38);
  gfx::fonts::draw_centred(frame, small, 55, date_text(m, o.month_first), kDate);
  if (!o.h24) gfx::fonts::draw(frame, small, 63 - gfx::fonts::width(small, m.meridiem()), 1, m.meridiem(), kDate);
}

}  // namespace p64::widgets::themed

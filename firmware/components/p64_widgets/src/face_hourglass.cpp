// The hourglass face: the hour as sand (assets/clock/hourglass: the walnut-and-brass frame
// with the glass outline, a two-tone sand tile). The top bulb holds what is left of the
// hour, filled from the neck up, its surface dipping into a funnel; the bottom bulb the
// heap that has run, rising to a peak under the neck; a one-pixel stream between them that
// loses a grain every third row, moving with the second when the seconds setting is on
// (all grains when off). The hour and the minute stacked beside it in the flip's numerals,
// the date under them in the smallest font.
#include "clock_assets.hpp"
#include "p64/gfx/fonts.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kBg{10, 10, 18}, kInk{226, 214, 190}, kDim{110, 104, 96};
constexpr int kX = 3, kY = 6;  // the frame's top-left; the bulbs' axis at kX + 12
constexpr int kAxis = kX + 12;
// The bulbs' half-widths per frame row: the top bulb rows 5..25, the neck 26, the bottom
// bulb 27..46 (9 - 8 * t^2.2 rounded, t along the bulb; the mock has the same numbers).
constexpr int kTopRow0 = 5, kNeckRow = 26, kBottomRow0 = 27, kFloorRow = 47;
constexpr int kTopWidths[21] = {9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 6, 6, 5, 5, 4, 3, 3, 2, 1};
constexpr int kBottomWidths[20] = {1, 2, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};

void sand_at(Frame &frame, int x, int y) {
  frame.set(x, y, sprite::view(assets::kHourglassSand).colour(x % 4, y % 4));
}

void sand_row(Frame &frame, int row, int hw) {
  for (int x = kAxis - hw; x <= kAxis + hw; ++x) sand_at(frame, x, kY + row);
}

}  // namespace

void draw_hourglass(Frame &frame, const Moment &m, const Options &o) {
  frame.clear(kBg);
  sprite::blit(frame, sprite::view(assets::kHourglassFrame), kX, kY);
  const int elapsed = m.minute * 60 + (o.seconds ? m.second : 0);  // seconds into the hour
  // the top bulb, filled from the neck up by what is left of the hour
  int area = 0;
  for (int hw : kTopWidths) area += 2 * hw + 1;
  int filled = 0, surface_row = -1, surface_hw = 0;
  for (int i = 20; i >= 0; --i) {
    if (filled * 3600 >= area * (3600 - elapsed)) break;
    sand_row(frame, kTopRow0 + i, kTopWidths[i]);
    filled += 2 * kTopWidths[i] + 1;
    surface_row = kTopRow0 + i;
    surface_hw = kTopWidths[i];
  }
  if (surface_row >= 0 && surface_hw >= 2) frame.fill_rect(kAxis - 1, kY + surface_row, 3, 1, kBg);  // the funnel
  // the bottom bulb: a heap from the floor up by what has run, peaked under the neck
  area = 0;
  for (int hw : kBottomWidths) area += 2 * hw + 1;
  filled = 0;
  int heap_top = kFloorRow;
  for (int i = 19; i >= 0; --i) {
    if (filled * 3600 >= area * elapsed) break;
    sand_row(frame, kBottomRow0 + i, kBottomWidths[i]);
    filled += 2 * kBottomWidths[i] + 1;
    heap_top = kBottomRow0 + i;
  }
  if (elapsed > 0) {
    if (heap_top - 1 > kBottomRow0) sand_row(frame, heap_top - 1, 2);
    if (heap_top - 2 > kBottomRow0) sand_row(frame, heap_top - 2, 0);
    heap_top -= 2;
  }
  // the stream
  const int stream_end = heap_top < 46 ? heap_top : 46;
  for (int row = kNeckRow; row < stream_end; ++row)
    if (!o.seconds || (row + m.second) % 3 != 0) sand_at(frame, kAxis, kY + row);
  // the readout
  const std::string hh = hour_text(m, o), mm = (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  for (int line = 0; line < 2; ++line) {
    const std::string &text = line ? mm : hh;
    int x = 40;
    for (char ch : text) {
      if (ch != ' ') sprite::stamp(frame, sprite::cell(assets::kFlipDigits, 10, ch - '0'), x, line ? 27 : 7, kInk);
      x += 11;
    }
  }
  const gfx::fonts::Font *small = gfx::fonts::by_name("everyday-slight");
  if (!small) small = &gfx::fonts::default_font();
  const std::string day = o.h24 ? weekday_name(m.wday) : std::string(weekday_name(m.wday)) + " " + m.meridiem();
  const std::string date = o.month_first ? std::string(month_name(m.mon)) + " " + std::to_string(m.mday)
                                         : std::to_string(m.mday) + " " + month_name(m.mon);
  gfx::fonts::draw(frame, *small, 49 - gfx::fonts::width(*small, day) / 2, 48, day, kDim);
  gfx::fonts::draw(frame, *small, 49 - gfx::fonts::width(*small, date) / 2, 55, date, kDim);
}

}  // namespace p64::widgets::themed

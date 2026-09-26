// The flip face: two split-flap tiles (assets/clock/flip: the tile, ten 9x16 numerals),
// the weekday and the date above in the small font, an amber seconds rail below. A change
// is ten frames: the old upper leaf falls (its rows squashed towards the hinge, darkening
// as it tilts from the light), then the new lower leaf lands (growing from the hinge,
// lit). Every pixel of a tile comes from one function of (x, y), so no tile image is
// kept: the frames are drawn straight onto the Frame.
#include "clock_assets.hpp"
#include "p64/gfx/fonts.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kInk{246, 238, 220}, kDim{120, 118, 112}, kBg{6, 6, 8}, kHinge{10, 10, 12};
constexpr Rgb kRailLit{214, 150, 40}, kRailUnlit{34, 30, 24};
constexpr int kTileW = 30, kTileH = 34, kLeaf = 17;
constexpr int kTileX[2] = {2, 33}, kTileY = 15;
constexpr int kFall[5] = {15, 12, 8, 4, 1};
constexpr int kLand[5] = {2, 6, 10, 14, 16};

// The tile's pixel at (x, y) with its two numerals stamped on and the hinge over them;
// false where the tile is transparent (its rounded corners).
bool tile_pixel(const std::string &text, int x, int y, Rgb &out) {
  const sprite::View tile = sprite::view(assets::kFlipTile);
  if (!tile.inked(x, y)) return false;
  out = tile.colour(x, y);
  for (int i = 0; i < 2; ++i) {
    const int gx = x - (5 + 11 * i), gy = y - 9;
    if (gx < 0 || gx >= 9 || gy < 0 || gy >= 16 || text[i] == ' ') continue;
    if (sprite::cell(assets::kFlipDigits, 10, text[i] - '0').inked(gx, gy)) out = kInk;
  }
  if (y == 16 && x >= 2) out = kHinge;
  return true;
}

// The tile's rows [row0, row1) at (x0, kTileY).
void draw_tile(Frame &frame, int x0, const std::string &text, int row0 = 0, int row1 = kTileH) {
  Rgb c;
  for (int y = row0; y < row1; ++y)
    for (int x = 0; x < kTileW; ++x)
      if (tile_pixel(text, x, y, c)) frame.set(x0 + x, kTileY + y, c);
}

// The leaf's 17 rows from src_y0 squashed to h rows at (x0 + 1, y0), shaded by pct %.
void draw_leaf(Frame &frame, int x0, int y0, const std::string &text, int src_y0, int h, int pct) {
  Rgb c;
  for (int y = 0; y < h; ++y) {
    const int sy = src_y0 + ((2 * y + 1) * kLeaf) / (2 * h);
    for (int x = 1; x < kTileW - 1; ++x) {
      if (!tile_pixel(text, x, sy, c)) continue;
      const auto shade = [pct](uint8_t v) { return static_cast<uint8_t>(std::min(255, (v * pct + 50) / 100)); };
      frame.set(x0 + x, y0 + y, {shade(c.r), shade(c.g), shade(c.b)});
    }
  }
}

}  // namespace

void draw_flip(Frame &frame, const Moment &m, const Options &o, int phase, const Moment *from) {
  frame.clear(kBg);
  const gfx::fonts::Font &small = gfx::fonts::default_font();
  const std::string now[2] = {hour_text(m, o), (m.minute < 10 ? "0" : "") + std::to_string(m.minute)};
  const std::string old[2] = {from ? hour_text(*from, o) : now[0],
                              from ? (from->minute < 10 ? "0" : "") + std::to_string(from->minute) : now[1]};
  for (int i = 0; i < 2; ++i) {
    const int x0 = kTileX[i];
    const int k = i == 1 ? phase : std::max(0, phase - 1);  // the hour tile one frame behind
    if (phase <= 0 || phase > kFlipFrames || old[i] == now[i]) {
      draw_tile(frame, x0, now[i]);
    } else if (k == 0) {
      draw_tile(frame, x0, old[i]);
    } else {
      draw_tile(frame, x0, now[i]);
      draw_tile(frame, x0, old[i], kLeaf, kTileH);  // the old lower leaf still shows
      if (k <= 5) {
        const int h = kFall[k - 1];
        draw_leaf(frame, x0, kTileY + kLeaf - h, old[i], 0, h, 100 - (55 * (kLeaf - h)) / kLeaf);
      } else {
        const int h = kLand[k - 6];
        draw_leaf(frame, x0, kTileY + kLeaf, now[i], kLeaf, h, 100 + (50 * (kLeaf - h)) / kLeaf);
      }
    }
  }
  gfx::fonts::draw(frame, small, 3, 4, weekday_name(m.wday), kDim);
  const std::string date = o.month_first ? std::string(month_name(m.mon)) + " " + std::to_string(m.mday)
                                         : std::to_string(m.mday) + " " + month_name(m.mon);
  gfx::fonts::draw(frame, small, 61 - gfx::fonts::width(small, date), 4, date, kDim);
  for (int i = 0; i < 30; ++i) frame.set(3 + i * 2, 54, o.seconds && i < m.second / 2 ? kRailLit : kRailUnlit);
  if (!o.h24) gfx::fonts::draw_centred(frame, small, 57, m.meridiem(), kDim);
}

}  // namespace p64::widgets::themed

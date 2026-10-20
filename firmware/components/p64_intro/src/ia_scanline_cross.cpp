// scanline_cross: a cathode-ray reveal, twice. A bright scan bar sweeps down the panel
// and leaves the picture lit behind it; a second bar sweeps up through the wordmark
// from the bottom. Each bar is two bright rows with dimmer rows either side, the full
// width of the screen, in white on a dark background and black on a light one.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void bar(Frame &f, int y, Rgb c) {
  f.fill_rect(0, y, Frame::width(), 2, c);
  f.blend_rect(0, y - 1, Frame::width(), 1, c, 110);
  f.blend_rect(0, y + 2, Frame::width(), 1, c, 110);
}

void render(Frame &f, const Context &ctx, float u) {
  const Rgb c = contrast(ctx.background);
  const Layer &a = L0();
  const float p = window(u, 0.0f, 0.5f);
  const float y_down = a.y - 2 + (a.h + 4) * p;
  draw_if(f, a, [&](int, int ly) { return a.y + ly < y_down; });
  const Layer &b = L1();
  const float q = window(u, 0.45f, 0.85f);
  const float y_up = b.y + b.h + 2 - (b.h + 4) * q;
  draw_if(f, b, [&](int, int ly) { return b.y + ly > y_up; });
  if (p < 1.0f) bar(f, px(y_down) - 1, c);
  if (q > 0.0f && q < 1.0f) bar(f, px(y_up) - 1, c);
}

}  // namespace

extern const Anim kScanlineCross{"scanline_cross", render};

}  // namespace p64::intro

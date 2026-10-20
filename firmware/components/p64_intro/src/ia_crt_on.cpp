// crt_on: an old television warming up. A bright horizontal line appears across the
// middle of where the panel will be and grows to its width; the picture then expands
// vertically out of the line, brightening as it opens; the wordmark follows the same way
// turned on its side, a vertical line that grows tall and then opens sideways.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Rgb lit = contrast(ctx.background);
  const Layer &a = L0();
  const float cy = centre_y(a), cx = centre_x(a);
  const float line = ease_out_cubic(window(u, 0.0f, 0.3f));
  const float open = ease_out_cubic(window(u, 0.3f, 0.72f));
  if (open > 0.0f) {
    const int dh = px(a.h * open);
    draw_resampled(f, a, a.x, px(cy - dh * 0.5f), a.w, dh, 0.5f + 0.5f * open);
  }
  if (open < 1.0f) {
    const int w = px(a.w * line);
    f.blend_rect(px(cx - w * 0.5f), px(cy) - 1, w, 2, lit, byte(1.0f - open));
  }
  const Layer &b = L1();
  const float bx = centre_x(b), by = centre_y(b);
  const float line1 = ease_out_cubic(window(u, 0.58f, 0.72f));
  const float open1 = ease_out_cubic(window(u, 0.72f, 0.94f));
  if (open1 > 0.0f) {
    const int dw = px(b.w * open1);
    draw_resampled(f, b, px(bx - dw * 0.5f), b.y, dw, b.h, 0.5f + 0.5f * open1);
  }
  if (line1 > 0.0f && open1 < 1.0f) {
    const int h = px(b.h * line1);
    f.blend_rect(px(bx) - 1, px(by - h * 0.5f), 2, h, lit, byte(1.0f - open1));
  }
}

}  // namespace

extern const Anim kCrtOn{"crt_on", render};

}  // namespace p64::intro

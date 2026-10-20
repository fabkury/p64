// curtain_banner: a stage. The curtains part from the middle of the panel, a bright
// seam at each edge as they go, until the whole picture is in view; a banner with the
// wordmark then unrolls down over it, a bright roll along its lower edge until the last
// row is out.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Rgb edge = scale(contrast(ctx.background), 0.6f);
  const Layer &a = L0();
  const float half = a.w * 0.5f;
  const float open = (half + 1.0f) * ease_out_cubic(window(u, 0.0f, 0.6f));
  draw_if(f, a, [&](int lx, int) { return std::fabs(lx + 0.5f - half) < open; });
  if (open < half) {
    const int left = px(a.x + half - open), right = px(a.x + half + open) - 1;
    f.fill_rect(left, a.y, 1, a.h, edge);
    f.fill_rect(right, a.y, 1, a.h, edge);
  }
  const Layer &b = L1();
  const float q = ease_out_cubic(window(u, 0.45f, 0.9f));
  const int rows = px(b.h * q);
  draw_if(f, b, [&](int, int ly) { return ly < rows; });
  if (q < 1.0f) f.fill_rect(b.x, b.y + rows, b.w, 2, contrast(ctx.background));
}

}  // namespace

extern const Anim kCurtainBanner{"curtain_banner", render};

}  // namespace p64::intro

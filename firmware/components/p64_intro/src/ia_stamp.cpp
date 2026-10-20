// stamp: the panel fades up quietly; then the wordmark comes down on it like a rubber
// stamp, from four times its size, accelerating, and flashes white on impact before the
// colours settle.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  draw_alpha(f, L0(), byte(smoothstep(window(u, 0.0f, 0.5f))));
  if (u < 0.4f) return;
  const Layer &b = L1();
  const float s = 1.0f + 3.0f * (1.0f - ease_in_cubic(window(u, 0.4f, 0.75f)));
  if (s > 1.0f) {
    const int dw = px(b.w * s), dh = px(b.h * s);
    draw_resampled(f, b, px(centre_x(b) - dw * 0.5f), px(centre_y(b) - dh * 0.5f), dw, dh);
    return;
  }
  draw_tinted(f, b, contrast(ctx.background), 1.0f - window(u, 0.75f, 0.86f));
}

}  // namespace

extern const Anim kStamp{"stamp", render};

}  // namespace p64::intro

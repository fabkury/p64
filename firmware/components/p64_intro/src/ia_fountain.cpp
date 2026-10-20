// fountain: the panel rises from below the bottom edge and settles; the wordmark's
// pixels are tossed up from the bottom of the screen one after another, each on its own
// parabola, and come down exactly where they belong.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const int below = Frame::height() - a.y;
  draw(f, a, 0, px(below * (1.0f - ease_out_cubic(window(u, 0.0f, 0.5f)))));
  const Layer &b = L1();
  each(b, [&](int lx, int ly, Rgb c) {
    const uint32_t h = hash(ctx.seed, lx, ly);
    const float launch = 0.35f + 0.3f * unit(h);
    if (u < launch) return;
    const float tau = window(u, launch, launch + 0.25f);
    const float home_x = static_cast<float>(b.x + lx), home_y = static_cast<float>(b.y + ly);
    const float from_x = home_x + (unit(h >> 8) - 0.5f) * 30.0f;
    const float apex = 12.0f + 14.0f * unit(h >> 16);
    const float x = lerp(from_x, home_x, tau);
    const float y = lerp(static_cast<float>(Frame::height()), home_y, tau) - apex * 4.0f * tau * (1.0f - tau);
    f.set(px(x), px(y), c);
  });
}

}  // namespace

extern const Anim kFountain{"fountain", render};

}  // namespace p64::intro

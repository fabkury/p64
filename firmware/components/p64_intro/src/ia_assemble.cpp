// assemble: the panel's pixels fly in from all around, each along the line from its home
// out through the panel's centre, from its own distance and with its own small delay, and
// decelerate into place; the wordmark's pixels streak in from beyond the right edge in
// a staggered swarm.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const float cx = a.w * 0.5f, cy = a.h * 0.5f;
  each(a, [&](int lx, int ly, Rgb c) {
    const uint32_t h = hash(ctx.seed, lx, ly);
    const float delay = unit(h) * 0.2f;
    const float p = ease_out_cubic(window(u, delay, delay + 0.55f));
    float dx = lx + 0.5f - cx, dy = ly + 0.5f - cy;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) {
      dx = 1.0f;
      dy = 0.0f;
    } else {
      dx /= len;
      dy /= len;
    }
    const float dist = (40.0f + 30.0f * unit(h >> 7)) * (1.0f - p);
    f.set(a.x + lx + px(dx * dist), a.y + ly + px(dy * dist), c);
  });
  const Layer &b = L1();
  each(b, [&](int lx, int ly, Rgb c) {
    const uint32_t h = hash(ctx.seed ^ 0xA55A0000u, lx, ly);
    const float delay = 0.3f + unit(h) * 0.25f;
    const float p = ease_out_cubic(window(u, delay, delay + 0.35f));
    const float start = static_cast<float>(Frame::width() - b.x) + 20.0f * unit(h >> 9);
    f.set(b.x + lx + px(start * (1.0f - p)), b.y + ly, c);
  });
}

}  // namespace

extern const Anim kAssemble{"assemble", render};

}  // namespace p64::intro

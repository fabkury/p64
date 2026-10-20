// channel_emerge: the panel arrives as three ghosts, its red plane from the left, its
// green from the right and its blue from above, each carrying only its own channel; where
// they overlap the true colours recombine, and they lock in one after the other. The
// wordmark then surfaces as a flat white (or black, on a light background) silhouette
// that takes on its rainbow.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void ghost(Frame &f, const Layer &l, int channel, int ox, int oy) {
  each(l, [&](int lx, int ly, Rgb c) {
    const int x = l.x + ox + lx, y = l.y + oy + ly;
    if (x < 0 || y < 0 || x >= Frame::width() || y >= Frame::height()) return;
    Rgb d = f.get(x, y);
    (channel == 0 ? d.r : channel == 1 ? d.g : d.b) = (channel == 0 ? c.r : channel == 1 ? c.g : c.b);
    f.set(x, y, d);
  });
}

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const int from_left = -(a.x + a.w), from_right = Frame::width() - a.x, from_top = -(a.y + a.h);
  ghost(f, a, 0, px(from_left * (1.0f - ease_out_cubic(window(u, 0.0f, 0.55f)))), 0);
  ghost(f, a, 1, px(from_right * (1.0f - ease_out_cubic(window(u, 0.05f, 0.6f)))), 0);
  ghost(f, a, 2, 0, px(from_top * (1.0f - ease_out_cubic(window(u, 0.1f, 0.65f)))));
  const Rgb flat = contrast(ctx.background);
  const uint8_t alpha = byte(smoothstep(window(u, 0.5f, 0.7f)));
  const float k = 1.0f - smoothstep(window(u, 0.7f, 0.95f));
  if (alpha == 0) return;
  const Layer &b = L1();
  each(b, [&](int lx, int ly, Rgb c) { f.blend(b.x + lx, b.y + ly, mix(c, flat, k), alpha); });
}

}  // namespace

extern const Anim kChannelEmerge{"channel_emerge", render};

}  // namespace p64::intro

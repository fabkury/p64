// swirl_orbit: a galaxy gathers. Every pixel of the panel spirals in along a curved
// polar arc about the panel's centre, its radius shrinking from far out while its angle
// unwinds just over a turn (the direction is the seed's choice); the wordmark, meanwhile,
// orbits the middle of the screen as one piece, fading in on a tightening circle, and
// lands on its line as the spiral closes.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr float kTau = 6.28318530718f;

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const float cx = a.w * 0.5f, cy = a.h * 0.5f;
  const float dir = (ctx.seed & 1u) ? 1.0f : -1.0f;
  const float p = ease_out_cubic(window(u, 0.0f, 0.72f));
  each(a, [&](int lx, int ly, Rgb c) {
    const float hx = lx + 0.5f - cx, hy = ly + 0.5f - cy;
    const float r0 = std::sqrt(hx * hx + hy * hy);
    const float a0 = std::atan2(hy, hx);
    const float launch = r0 + 48.0f * (0.7f + 0.6f * chance(ctx.seed, lx, ly));
    const float r = r0 + (launch - r0) * (1.0f - p);
    const float ang = a0 + dir * 1.1f * kTau * (1.0f - p);
    // The offset from home, so a finished spiral lands exactly on the pixel.
    const float ox = r * std::cos(ang) - r0 * std::cos(a0), oy = r * std::sin(ang) - r0 * std::sin(a0);
    f.set(a.x + lx + px(ox), a.y + ly + px(oy), c);
  });
  const Layer &b = L1();
  const float q = ease_out_cubic(window(u, 0.25f, 0.9f));
  const float mx = Frame::width() * 0.5f, my = Frame::height() * 0.5f;
  const float bx = centre_x(b) - mx, by = centre_y(b) - my;
  const float r0 = std::sqrt(bx * bx + by * by), a0 = std::atan2(by, bx);
  const float r = r0 + 30.0f * (1.0f - q), ang = a0 - dir * 2.0f * kTau * (1.0f - q);
  const int ox = px(r * std::cos(ang) - r0 * std::cos(a0)), oy = px(r * std::sin(ang) - r0 * std::sin(a0));
  draw_alpha(f, b, byte(smoothstep(window(u, 0.25f, 0.45f))), ox, oy);
}

}  // namespace

extern const Anim kSwirlOrbit{"swirl_orbit", render};

}  // namespace p64::intro

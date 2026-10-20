// swirl_orbit: a galaxy gathers. Every pixel of the panel spirals in about the panel's
// centre, from far out, its angle unwinding just over a turn (the direction is the seed's
// choice) while its distance shrinks to its own; the wordmark, meanwhile, orbits the
// middle of the screen as one piece, fading in on a tightening circle, and lands on its
// line as the spiral closes.
//
// Cost: the spiral is one rotation and one radial scale shared by every pixel of a
// frame (one sine and cosine per frame), plus a per-pixel scatter of the launch distance
// from the hash; no per-pixel trigonometry (the first version did, at 12 ms a frame on
// the device, 2026-10-20).
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr float kTau = 6.28318530718f;

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const float cx = a.w * 0.5f, cy = a.h * 0.5f;
  const float dir = (ctx.seed & 1u) ? 1.0f : -1.0f;
  const float p = ease_out_cubic(window(u, 0.0f, 0.72f));
  const float remain = 1.0f - p;
  const float ang = dir * 1.1f * kTau * remain;
  const float cs = std::cos(ang), sn = std::sin(ang);
  each(a, [&](int lx, int ly, Rgb c) {
    const float hx = lx + 0.5f - cx, hy = ly + 0.5f - cy;
    // Launched 2.5 to 4 times as far out as home, scattered per pixel; back to 1 at p = 1.
    const float grow = 1.0f + (1.5f + 1.5f * chance(ctx.seed, lx, ly)) * remain;
    const float rx = (hx * cs - hy * sn) * grow, ry = (hx * sn + hy * cs) * grow;
    f.set(a.x + lx + px(rx - hx), a.y + ly + px(ry - hy), c);
  });
  const Layer &b = L1();
  const float q = ease_out_cubic(window(u, 0.25f, 0.9f));
  const float mx = Frame::width() * 0.5f, my = Frame::height() * 0.5f;
  const float bx = centre_x(b) - mx, by = centre_y(b) - my;
  const float r0 = std::sqrt(bx * bx + by * by), a0 = std::atan2(by, bx);
  const float r = r0 + 30.0f * (1.0f - q), orbit = a0 - dir * 2.0f * kTau * (1.0f - q);
  const int ox = px(r * std::cos(orbit) - r0 * std::cos(a0)), oy = px(r * std::sin(orbit) - r0 * std::sin(a0));
  draw_alpha(f, b, byte(smoothstep(window(u, 0.25f, 0.45f))), ox, oy);
}

}  // namespace

extern const Anim kSwirlOrbit{"swirl_orbit", render};

}  // namespace p64::intro

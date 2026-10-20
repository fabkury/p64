// starburst_sparkle: the panel's pixels come flying in from far outside along their own
// radial lines, overshoot their homes and snap back; the wordmark then ignites pixel by
// pixel in random order, each a white spark for an instant before cooling to its colour.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const float cx = a.w * 0.5f, cy = a.h * 0.5f;
  each(a, [&](int lx, int ly, Rgb c) {
    const uint32_t h = hash(ctx.seed, lx, ly);
    const float delay = unit(h) * 0.15f;
    const float p = ease_out_back(window(u, delay, delay + 0.5f), 2.6f);
    float dx = lx + 0.5f - cx, dy = ly + 0.5f - cy;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) {
      dx = 1.0f;
      dy = 0.0f;
    } else {
      dx /= len;
      dy /= len;
    }
    const float dist = 60.0f * (0.8f + 0.4f * unit(h >> 6)) * (1.0f - p);
    f.set(a.x + lx + px(dx * dist), a.y + ly + px(dy * dist), c);
  });
  const Layer &b = L1();
  const Rgb spark = contrast(ctx.background);
  each(b, [&](int lx, int ly, Rgb c) {
    const float ignite = 0.4f + 0.35f * chance(ctx.seed ^ 0x5A4Bu, lx, ly);
    if (u < ignite) return;
    const float cool = window(u, ignite + 0.08f, ignite + 0.18f);
    f.set(b.x + lx, b.y + ly, cool >= 1.0f ? c : mix(spark, c, cool));
  });
}

}  // namespace

extern const Anim kStarburstSparkle{"starburst_sparkle", render};

}  // namespace p64::intro

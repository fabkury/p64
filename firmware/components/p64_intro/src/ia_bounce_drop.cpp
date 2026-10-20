// bounce_drop: gravity. The panel falls from above the top edge, accelerating, hits its
// line, rebounds twice with energy loss and settles; the wordmark then drops from the top
// onto the panel and takes one bounce of its own. Analytical arcs, no integration: the
// fall is y = H (1 - w^2) so the speed grows linearly into the impact, each bounce is a
// symmetric parabola whose height is the restitution squared times the previous one.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

// The height above home (pixels, >= 0) at progress w in [0, 1] of a drop from `height`
// with `bounces` rebounds and restitution `e`; 0 from 0.92 on.
float drop_height(float w, float height, int bounces, float e) {
  if (w <= 0.0f) return height;
  // Arc durations are proportional to the square root of their height: 1, 2e, 2e^2, ...
  float spans[4] = {1.0f, 2.0f * e, 2.0f * e * e, 2.0f * e * e * e};
  float total = 0.0f;
  for (int i = 0; i <= bounces; ++i) total += spans[i];
  const float scale = 0.92f / total;  // the last 8 % rest at home
  float start = 0.0f;
  float h = height;
  for (int i = 0; i <= bounces; ++i) {
    const float end = start + spans[i] * scale;
    if (w < end) {
      const float v = (w - start) / (end - start);
      return i == 0 ? h * (1.0f - v * v) : h * 4.0f * v * (1.0f - v);
    }
    start = end;
    h *= e * e;
  }
  return 0.0f;
}

void render(Frame &f, const Context &, float u) {
  const float h0 = static_cast<float>(L0().y + L0().h);
  const float h1 = static_cast<float>(L1().y + L1().h);
  draw(f, L0(), 0, -px(drop_height(window(u, 0.0f, 0.62f), h0, 2, 0.42f)));
  if (u >= 0.5f) draw(f, L1(), 0, -px(drop_height(window(u, 0.5f, 0.93f), h1, 1, 0.35f)));
}

}  // namespace

extern const Anim kBounceDrop{"bounce_drop", render};

}  // namespace p64::intro

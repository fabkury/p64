// spotlight_sweep: a spotlight wanders over the panel in the dark, showing a soft disc
// of the picture at a time, then widens until everything is lit; a beam of light then
// sweeps along the wordmark from left to right, the letters brightening to white as it
// passes and settling behind it.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  // The spot's path: a figure over the picture, returning to its centre as it widens.
  const float roam = 1.0f - smoothstep(window(u, 0.45f, 0.75f));
  const float sx = a.w * 0.5f + roam * 13.0f * std::sin(u * 9.0f), sy = a.h * 0.5f + roam * 12.0f * std::cos(u * 7.0f);
  const float radius = 11.0f + 70.0f * smoothstep(window(u, 0.45f, 0.78f));
  const float ambient = smoothstep(window(u, 0.55f, 0.8f));
  each(a, [&](int lx, int ly, Rgb c) {
    const float dx = lx + 0.5f - sx, dy = ly + 0.5f - sy;
    const float d = std::sqrt(dx * dx + dy * dy);
    float k = 1.0f - d / radius;
    if (k < ambient) k = ambient;
    if (k <= 0.0f) return;
    f.set(a.x + lx, a.y + ly, k >= 1.0f ? c : scale(c, k));
  });
  const Layer &b = L1();
  const uint8_t alpha = byte(smoothstep(window(u, 0.55f, 0.8f)));
  if (alpha == 0) return;
  const Rgb lit = contrast(ctx.background);
  const float head = -4.0f + (b.w + 8.0f) * window(u, 0.75f, 0.93f);
  each(b, [&](int lx, int ly, Rgb c) {
    const float d = std::fabs(lx + 0.5f - head);
    f.blend(b.x + lx, b.y + ly, d < 4.0f ? mix(c, lit, 1.0f - d / 4.0f) : c, alpha);
  });
}

}  // namespace

extern const Anim kSpotlightSweep{"spotlight_sweep", render};

}  // namespace p64::intro

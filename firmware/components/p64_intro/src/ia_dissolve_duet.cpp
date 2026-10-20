// dissolve_duet: a pixel dissolve, one layer after the other. Every pixel of the panel
// draws a fixed number from the seed and appears once the rising threshold passes it, a
// speckle that fills in; the wordmark dissolves in the same way from a different draw,
// starting while the panel is still half there.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const float p0 = smoothstep(window(u, 0.0f, 0.65f));
  draw_if(f, L0(), [&](int lx, int ly) { return chance(ctx.seed, lx, ly) < p0; });
  const float p1 = smoothstep(window(u, 0.3f, 0.93f));
  draw_if(f, L1(), [&](int lx, int ly) { return chance(ctx.seed ^ 0x51515151u, lx, ly) < p1; });
}

}  // namespace

extern const Anim kDissolveDuet{"dissolve_duet", render};

}  // namespace p64::intro

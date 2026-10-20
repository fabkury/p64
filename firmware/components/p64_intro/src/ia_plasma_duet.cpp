// plasma_duet: an organic reveal. A smooth plasma field (three sine waves with phases
// from the seed) covers each layer, and a pixel shows once the rising threshold passes
// its value, so the panel emerges in soft blobs that grow and join rather than specks;
// the wordmark follows through a finer, differently phased field.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

// A field in [0, 0.98]: three sines of the given frequencies and seed-chosen phases.
float field(const Context &ctx, uint32_t key, int x, int y, float fx, float fy, float fd) {
  const float p1 = 6.2831853f * chance(ctx.seed, key, 1u), p2 = 6.2831853f * chance(ctx.seed, key, 2u),
              p3 = 6.2831853f * chance(ctx.seed, key, 3u);
  const float s = std::sin(x * fx + p1) + std::sin(y * fy + p2) + std::sin((x + y) * fd + p3);
  return (s / 3.0f + 1.0f) * 0.49f;
}

void render(Frame &f, const Context &ctx, float u) {
  const float p0 = smoothstep(window(u, 0.0f, 0.7f));
  draw_if(f, L0(), [&](int lx, int ly) { return field(ctx, 0xA0u, lx, ly, 0.35f, 0.3f, 0.2f) <= p0; });
  const float p1 = smoothstep(window(u, 0.35f, 0.93f));
  draw_if(f, L1(), [&](int lx, int ly) { return field(ctx, 0xA1u, lx, ly, 0.5f, 0.45f, 0.3f) <= p1; });
}

}  // namespace

extern const Anim kPlasmaDuet{"plasma_duet", render};

}  // namespace p64::intro

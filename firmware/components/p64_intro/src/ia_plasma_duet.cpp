// plasma_duet: an organic reveal. A smooth plasma field (three sine waves with phases
// from the seed) covers each layer, and a pixel shows once the rising threshold passes
// its value, so the panel emerges in soft blobs that grow and join rather than specks;
// the wordmark follows through a finer, differently phased field.
//
// Cost: the three waves depend on x, on y and on x + y, so each is a table filled once a
// frame (a few hundred sines) and a pixel is three lookups (per-pixel sines cost 12 ms a
// frame on the device, 2026-10-20).
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr float kTau = 6.28318530718f;

// Reveals the layer where the field is at or below `threshold`.
void reveal(Frame &f, const Context &ctx, const Layer &l, uint32_t key, float fx, float fy, float fd, float threshold) {
  if (threshold <= 0.0f) return;
  float wx[64], wy[64], wd[128];
  const float p1 = kTau * chance(ctx.seed, key, 1u), p2 = kTau * chance(ctx.seed, key, 2u), p3 = kTau * chance(ctx.seed, key, 3u);
  for (int x = 0; x < l.w; ++x) wx[x] = std::sin(x * fx + p1);
  for (int y = 0; y < l.h; ++y) wy[y] = std::sin(y * fy + p2);
  for (int d = 0; d < l.w + l.h; ++d) wd[d] = std::sin(d * fd + p3);
  // The field in [0, 0.98]: the three sines averaged, mapped from [-1, 1].
  draw_if(f, l, [&](int lx, int ly) { return ((wx[lx] + wy[ly] + wd[lx + ly]) / 3.0f + 1.0f) * 0.49f <= threshold; });
}

void render(Frame &f, const Context &ctx, float u) {
  reveal(f, ctx, L0(), 0xA0u, 0.35f, 0.3f, 0.2f, smoothstep(window(u, 0.0f, 0.7f)));
  reveal(f, ctx, L1(), 0xA1u, 0.5f, 0.45f, 0.3f, smoothstep(window(u, 0.35f, 0.93f)));
}

}  // namespace

extern const Anim kPlasmaDuet{"plasma_duet", render};

}  // namespace p64::intro

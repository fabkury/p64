// tiles_diagonal: the panel pops in as four-by-four tiles in an order drawn from the
// seed, each tile a flat bright block for a blink before its pixels show; the wordmark
// is then wiped in along the diagonal from its bottom-left corner to its top-right, a
// lit edge leading the wipe.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr int kTile = 4;

void render(Frame &f, const Context &ctx, float u) {
  const Rgb lit = contrast(ctx.background);
  const Layer &a = L0();
  // Each tile's moment, spread over the first 60 %; a tile is a flat block for 0.05 after it.
  for (int ty = 0; ty * kTile < a.h; ++ty) {
    for (int tx = 0; tx * kTile < a.w; ++tx) {
      const float at = chance(ctx.seed, tx, ty) * 0.6f;
      if (u < at) continue;
      const bool block = u < at + 0.05f;
      for (int ly = ty * kTile; ly < (ty + 1) * kTile && ly < a.h; ++ly)
        for (int lx = tx * kTile; lx < (tx + 1) * kTile && lx < a.w; ++lx)
          if (inked(a, lx, ly)) f.set(a.x + lx, a.y + ly, block ? scale(lit, 0.7f) : colour(a, lx, ly));
    }
  }
  const Layer &b = L1();
  const float reach = static_cast<float>(b.w + b.h);
  const float edge = (reach + 3.0f) * window(u, 0.5f, 0.9f);  // along lx + (h - 1 - ly)
  each(b, [&](int lx, int ly, Rgb c) {
    const float d = edge - (lx + (b.h - 1 - ly));
    if (d <= 0.0f) return;
    f.set(b.x + lx, b.y + ly, d < 2.0f ? lit : c);
  });
}

}  // namespace

extern const Anim kTilesDiagonal{"tiles_diagonal", render};

}  // namespace p64::intro

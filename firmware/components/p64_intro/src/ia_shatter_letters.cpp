// shatter_letters: the panel is broken into sixteen irregular shards (a Voronoi
// partition drawn from the seed) that fly in from outside, each along its own outward
// line, fast at first and gently at the end; the wordmark follows as its three letters,
// the p from the left, the 6 from above, the 4 from the right.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr int kSites = 16;

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  int sx[kSites], sy[kSites];
  float fx[kSites], fy[kSites];
  const float cx = (a.w - 1) * 0.5f, cy = (a.h - 1) * 0.5f;
  for (int i = 0; i < kSites; ++i) {
    const uint32_t hx = hash(ctx.seed, i, 0xCA11u), hy = hash(ctx.seed, i, 0xC0DEu);
    sx[i] = static_cast<int>(hx % static_cast<uint32_t>(a.w));
    sy[i] = static_cast<int>(hy % static_cast<uint32_t>(a.h));
    float dx = sx[i] - cx, dy = sy[i] - cy;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) {
      dx = 1.0f;
      dy = 0.0f;
      len = 1.0f;
    }
    const float reach = 36.0f * (0.8f + 0.5f * unit(hx >> 5));
    fx[i] = dx / len * reach;
    fy[i] = dy / len * reach;
  }
  const float v = 1.0f - window(u, 0.0f, 0.7f);
  const float remain = v * v * v;
  uint8_t *cell = ctx.scratch;  // a.w * a.h <= kScratchBytes
  if (cell) {
    for (int ly = 0; ly < a.h; ++ly) {
      for (int lx = 0; lx < a.w; ++lx) {
        int best = 0, best_d = 1 << 30;
        for (int i = 0; i < kSites; ++i) {
          const int d = (lx - sx[i]) * (lx - sx[i]) + (ly - sy[i]) * (ly - sy[i]);
          if (d < best_d) {
            best_d = d;
            best = i;
          }
        }
        cell[ly * a.w + lx] = static_cast<uint8_t>(best);
      }
    }
    each(a, [&](int lx, int ly, Rgb c) {
      const int i = cell[ly * a.w + lx];
      f.set(a.x + lx + px(fx[i] * remain), a.y + ly + px(fy[i] * remain), c);
    });
  } else {
    draw(f, a);  // no working memory: the panel simply shows
  }
  const Layer &b = L1();
  // The three letters by thirds of the width (the outline joins them, so by hand).
  const int cut1 = 15, cut2 = 28;
  const int from_left = -(b.x + cut1), from_top = -(b.y + b.h), from_right = Frame::width() - b.x - cut2;
  const int ox_p = px(from_left * (1.0f - ease_out_cubic(window(u, 0.45f, 0.8f))));
  const int oy_6 = px(from_top * (1.0f - ease_out_cubic(window(u, 0.55f, 0.88f))));
  const int ox_4 = px(from_right * (1.0f - ease_out_cubic(window(u, 0.65f, 0.93f))));
  each(b, [&](int lx, int ly, Rgb c) {
    if (lx < cut1) {
      f.set(b.x + lx + ox_p, b.y + ly, c);
    } else if (lx < cut2) {
      f.set(b.x + lx, b.y + ly + oy_6, c);
    } else {
      f.set(b.x + lx + ox_4, b.y + ly, c);
    }
  });
}

}  // namespace

extern const Anim kShatterLetters{"shatter_letters", render};

}  // namespace p64::intro

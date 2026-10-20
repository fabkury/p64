// bands_cross: the panel opens in vertical bands four pixels wide, each wiping down (the
// even ones) or up (the odd ones) a little after its neighbour; the wordmark then opens
// in horizontal bands four pixels tall, wiping right and left alternately.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr int kBand = 4;

void render(Frame &f, const Context &, float u) {
  const Layer &a = L0();
  draw_if(f, a, [&](int lx, int ly) {
    const int band = lx / kBand;
    const float p = ease_out_cubic(window(u, band * 0.03f, band * 0.03f + 0.45f));
    const float edge = a.h * p;
    return (band & 1) ? (a.h - ly) <= edge : (ly + 1) <= edge;
  });
  const Layer &b = L1();
  draw_if(f, b, [&](int lx, int ly) {
    const int band = ly / kBand;
    const float p = ease_out_cubic(window(u, 0.3f + band * 0.04f, 0.3f + band * 0.04f + 0.4f));
    const float edge = b.w * p;
    return (band & 1) ? (b.w - lx) <= edge : (lx + 1) <= edge;
  });
}

}  // namespace

extern const Anim kBandsCross{"bands_cross", render};

}  // namespace p64::intro

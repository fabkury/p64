// hue_spin: colour finds itself. The panel fades in with every hue turned, spinning twice
// round the colour wheel on its way to the true picture; the wordmark fades in a little
// later with its rainbow sliding along the letters, three turns, until the gradient
// comes to rest where it belongs. Greys (the outline, the bird) are untouched throughout.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void spin(Frame &f, const Layer &l, float turns, uint8_t alpha) {
  if (alpha == 0) return;
  each(l, [&](int lx, int ly, Rgb c) { f.blend(l.x + lx, l.y + ly, hue_rotate(c, turns), alpha); });
}

void render(Frame &f, const Context &, float u) {
  spin(f, L0(), 2.0f * (1.0f - smoothstep(window(u, 0.0f, 0.88f))), byte(smoothstep(window(u, 0.0f, 0.5f))));
  spin(f, L1(), 3.0f * (1.0f - smoothstep(window(u, 0.2f, 0.92f))), byte(smoothstep(window(u, 0.2f, 0.5f))));
}

}  // namespace

extern const Anim kHueSpin{"hue_spin", render};

}  // namespace p64::intro

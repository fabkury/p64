// slide_cross: the panel slides down from above the top edge and the wordmark slides up
// from below the bottom edge, crossing paths; the panel eases out cubically, the wordmark
// overshoots its line by a few pixels and settles back onto it.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &, float u) {
  const int top = -(L0().y + L0().h);  // fully above the canvas
  const int bottom = Frame::height() - L1().y;  // fully below it
  draw(f, L0(), 0, px(top * (1.0f - ease_out_cubic(window(u, 0.0f, 0.7f)))));
  draw(f, L1(), 0, px(bottom * (1.0f - ease_out_back(window(u, 0.2f, 0.9f)))));
}

}  // namespace

extern const Anim kSlideCross{"slide_cross", render};

}  // namespace p64::intro

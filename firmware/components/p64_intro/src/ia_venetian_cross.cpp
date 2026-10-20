// venetian_cross: venetian blinds in two directions. The panel arrives as horizontal
// strips four rows tall sliding in from the left and the right alternately, each a
// moment after the one above; the wordmark arrives as vertical strips four columns wide
// sliding in from the top and the bottom alternately.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr int kStrip = 4;

void render(Frame &f, const Context &, float u) {
  const Layer &a = L0();
  each(a, [&](int lx, int ly, Rgb c) {
    const int strip = ly / kStrip;
    const float p = ease_out_cubic(window(u, strip * 0.02f, strip * 0.02f + 0.5f));
    const int travel = px(Frame::width() * (1.0f - p));
    f.set(a.x + lx + ((strip & 1) ? travel : -travel), a.y + ly, c);
  });
  const Layer &b = L1();
  each(b, [&](int lx, int ly, Rgb c) {
    const int strip = lx / kStrip;
    const float p = ease_out_cubic(window(u, 0.35f + strip * 0.015f, 0.35f + strip * 0.015f + 0.4f));
    const int travel = px(Frame::height() * (1.0f - p));
    f.set(b.x + lx, b.y + ly + ((strip & 1) ? travel : -travel), c);
  });
}

}  // namespace

extern const Anim kVenetianCross{"venetian_cross", render};

}  // namespace p64::intro

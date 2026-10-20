// flip_card: a card turning to face you. The panel turns about its vertical axis from
// edge-on to flat, foreshortened and shaded as it comes round; the wordmark then turns
// about its horizontal axis, the same way, down onto the panel.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr float kHalfPi = 1.57079632679f;

void render(Frame &f, const Context &, float u) {
  const Layer &a = L0();
  const float p = ease_out_cubic(window(u, 0.0f, 0.6f));
  const float cos_a = std::cos((1.0f - p) * kHalfPi);
  const int dw = px(a.w * cos_a);
  draw_resampled(f, a, px(centre_x(a) - dw * 0.5f), a.y, dw, a.h, 0.3f + 0.7f * cos_a);
  const Layer &b = L1();
  const float q = ease_out_cubic(window(u, 0.5f, 0.92f));
  const float cos_b = std::cos((1.0f - q) * kHalfPi);
  const int dh = px(b.h * cos_b);
  draw_resampled(f, b, b.x, px(centre_y(b) - dh * 0.5f), b.w, dh, 0.3f + 0.7f * cos_b);
}

}  // namespace

extern const Anim kFlipCard{"flip_card", render};

}  // namespace p64::intro

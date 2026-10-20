// iris_typewriter: a circular iris opens from the centre of the panel until the whole
// picture is in view; the wordmark is then typed in, column by column from the left,
// behind a blinking cursor bar that vanishes with the last column.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const Layer &a = L0();
  const float cx = a.w * 0.5f, cy = a.h * 0.5f;
  const float reach = std::sqrt(cx * cx + cy * cy) + 1.0f;
  const float r = reach * smoothstep(window(u, 0.0f, 0.6f));
  const float r2 = r * r;
  draw_if(f, a, [&](int lx, int ly) {
    const float dx = lx + 0.5f - cx, dy = ly + 0.5f - cy;
    return dx * dx + dy * dy < r2;
  });
  const Layer &b = L1();
  const float p = window(u, 0.45f, 0.9f);
  const int head = static_cast<int>(b.w * p);  // columns before the head are typed
  draw_if(f, b, [&](int lx, int) { return lx < head; });
  if (p < 1.0f) {
    // Six blinks over the typing; the cursor is a full-height bar at the head.
    const bool on = std::fmod(p * 6.0f, 1.0f) < 0.5f;
    if (on) f.fill_rect(b.x + head, b.y, 2, b.h, contrast(ctx.background));
  }
}

}  // namespace

extern const Anim kIrisTypewriter{"iris_typewriter", render};

}  // namespace p64::intro

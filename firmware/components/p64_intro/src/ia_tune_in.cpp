// tune_in: a station coming in. The panel starts as television snow in its own shape,
// grey noise changing every frame, and pixel by pixel the picture locks in; the
// wordmark then fades up in greys and its rainbow saturates into place.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &ctx, float u) {
  const uint32_t frame_key = static_cast<uint32_t>(u * 40.0f);  // the snow changes 40 times
  const Layer &a = L0();
  each(a, [&](int lx, int ly, Rgb c) {
    const float settle = 0.1f + 0.6f * chance(ctx.seed, lx, ly);
    if (u >= settle) {
      f.set(a.x + lx, a.y + ly, c);
      return;
    }
    const uint8_t g = static_cast<uint8_t>(hash(ctx.seed ^ frame_key, lx, ly) >> 24);
    f.set(a.x + lx, a.y + ly, {g, g, g});
  });
  const Layer &b = L1();
  const uint8_t alpha = byte(smoothstep(window(u, 0.45f, 0.7f)));
  if (alpha == 0) return;
  const float grey = 1.0f - smoothstep(window(u, 0.6f, 0.9f));
  each(b, [&](int lx, int ly, Rgb c) {
    const uint8_t l = byte(luma(c));
    f.blend(b.x + lx, b.y + ly, grey <= 0.0f ? c : mix(c, {l, l, l}, grey), alpha);
  });
}

}  // namespace

extern const Anim kTuneIn{"tune_in", render};

}  // namespace p64::intro

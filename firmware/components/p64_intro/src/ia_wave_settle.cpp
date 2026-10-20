// wave_settle: the panel fades in rippling, each row pushed sideways by a travelling
// sine wave whose amplitude dies away until the rows line up; the wordmark fades in with
// its columns bobbing up and down on a wave of its own, which settles a little later.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr float kTau = 6.28318530718f;

void render(Frame &f, const Context &, float u) {
  const Layer &a = L0();
  const float amp0 = 10.0f * (1.0f - smoothstep(window(u, 0.0f, 0.8f)));
  const uint8_t alpha0 = byte(smoothstep(window(u, 0.0f, 0.35f)));
  if (alpha0 > 0) {
    each(a, [&](int lx, int ly, Rgb c) {
      const int dx = px(amp0 * std::sin(kTau * (ly / 12.0f + u * 3.0f)));
      f.blend(a.x + lx + dx, a.y + ly, c, alpha0);
    });
  }
  const Layer &b = L1();
  const float amp1 = 6.0f * (1.0f - smoothstep(window(u, 0.3f, 0.9f)));
  const uint8_t alpha1 = byte(smoothstep(window(u, 0.3f, 0.55f)));
  if (alpha1 > 0) {
    each(b, [&](int lx, int ly, Rgb c) {
      const int dy = px(amp1 * std::sin(kTau * (lx / 10.0f + u * 4.0f)));
      f.blend(b.x + lx, b.y + ly + dy, c, alpha1);
    });
  }
}

}  // namespace

extern const Anim kWaveSettle{"wave_settle", render};

}  // namespace p64::intro

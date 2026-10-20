// glitch_settle: a bad signal that locks. The panel arrives in slabs four rows tall that
// jitter sideways, each on its own, with the red and blue channels pulled apart; the
// shake is quantised into steps so it stutters rather than slides, and dies away as the
// square of the remaining time. The wordmark flickers on and off, more on than off as
// time passes, jittering with it, until both hold still.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr int kSlab = 4;

// Writes one channel of a pixel.
void set_channel(Frame &f, int x, int y, int channel, uint8_t v) {
  if (x < 0 || y < 0 || x >= Frame::width() || y >= Frame::height()) return;
  Rgb c = f.get(x, y);
  (channel == 0 ? c.r : channel == 1 ? c.g : c.b) = v;
  f.set(x, y, c);
}

void render(Frame &f, const Context &ctx, float u) {
  const float decay = (1.0f - u) * (1.0f - u);
  const uint32_t step = static_cast<uint32_t>(u * 30.0f);  // the shake changes 30 times over the window
  const float amp = 12.0f * decay;
  const int split = px(3.0f * decay);  // chromatic separation
  const Layer &a = L0();
  each(a, [&](int lx, int ly, Rgb c) {
    const int slab = ly / kSlab;
    const int jitter = px(amp * (2.0f * chance(ctx.seed, slab, step) - 1.0f));
    const int x = a.x + lx + jitter, y = a.y + ly;
    if (split == 0) {
      f.set(x, y, c);
    } else {
      set_channel(f, x + split, y, 0, c.r);
      set_channel(f, x, y, 1, c.g);
      set_channel(f, x - split, y, 2, c.b);
    }
  });
  const float on_chance = window(u, 0.35f, 0.85f);
  const bool on = chance(ctx.seed ^ 0x600Du, step, 0x11u) < on_chance;
  if (on) {
    const int jitter = px(amp * 0.5f * (2.0f * chance(ctx.seed ^ 0x600Du, step, 0x22u) - 1.0f));
    draw(f, L1(), jitter, 0);
  }
}

}  // namespace

extern const Anim kGlitchSettle{"glitch_settle", render};

}  // namespace p64::intro

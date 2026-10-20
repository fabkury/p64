// fade_duet: the plainest reveal, in two voices. The panel fades in first; the wordmark
// starts fading in while the panel is still arriving and finishes last, so the logo
// assembles itself out of light rather than appearing all at once.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

void render(Frame &f, const Context &, float u) {
  draw_alpha(f, L0(), byte(smoothstep(window(u, 0.0f, 0.6f))));
  draw_alpha(f, L1(), byte(smoothstep(window(u, 0.35f, 0.92f))));
}

}  // namespace

extern const Anim kFadeDuet{"fade_duet", render};

}  // namespace p64::intro

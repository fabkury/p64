// blinds_flip: slats turning to face you. The panel is cut into eight strips six rows
// tall that open from edge-on, each growing about its own centre line with the picture
// foreshortened and shaded dark-to-bright as it turns, the top strip first; the wordmark
// then turns open as a single slat.
#include "intro_common.hpp"

namespace p64::intro {
namespace {

constexpr int kStrip = 6;

// Rows [row0, row0 + rows) of the layer resampled into `height` rows about their centre.
void slat(Frame &f, const Layer &l, int row0, int rows, int height, float shade) {
  if (height <= 0) return;
  const int y0 = l.y + row0 + (rows - height) / 2;
  for (int y = 0; y < height; ++y) {
    const int ly = row0 + (height >= rows ? y : y * rows / height);
    for (int lx = 0; lx < l.w; ++lx) {
      if (!inked(l, lx, ly)) continue;
      const Rgb c = colour(l, lx, ly);
      f.set(l.x + lx, y0 + y, shade >= 1.0f ? c : scale(c, shade));
    }
  }
}

void render(Frame &f, const Context &, float u) {
  const Layer &a = L0();
  for (int s = 0; s * kStrip < a.h; ++s) {
    const float p = ease_out_cubic(window(u, s * 0.07f, s * 0.07f + 0.4f));
    slat(f, a, s * kStrip, kStrip, px(kStrip * p), 0.35f + 0.65f * p);
  }
  const Layer &b = L1();
  const float q = ease_out_cubic(window(u, 0.55f, 0.92f));
  slat(f, b, 0, b.h, px(b.h * q), 0.35f + 0.65f * q);
}

}  // namespace

extern const Anim kBlindsFlip{"blinds_flip", render};

}  // namespace p64::intro

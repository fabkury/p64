#include "p64/widgets/notice.hpp"

#include "clock_assets.hpp"
#include "sprite.hpp"

namespace p64::widgets::notice {

void draw_sun(gfx::Frame &frame, uint8_t alpha) {
  if (alpha == 0) return;
  const sprite::View v = sprite::view(assets::kNoticeSun);
  // The ring box is the sprite plus one pixel on each side, centred (a 20 px sun: 21..42).
  const int x0 = (gfx::Frame::width() - (v.w + 2)) / 2;
  const int y0 = (gfx::Frame::height() - (v.h + 2)) / 2;
  const uint8_t ring = ring_alpha(alpha);
  const gfx::Rgb white{255, 255, 255};
  auto inked = [&](int sx, int sy) { return sx >= 0 && sy >= 0 && sx < v.w && sy < v.h && v.inked(sx, sy); };
  for (int y = -1; y <= v.h; ++y) {
    for (int x = -1; x <= v.w; ++x) {
      if (inked(x, y)) {
        frame.blend(x0 + 1 + x, y0 + 1 + y, v.colour(x, y), alpha);
        continue;
      }
      bool edge = false;
      for (int dy = -1; dy <= 1 && !edge; ++dy)
        for (int dx = -1; dx <= 1 && !edge; ++dx) edge = (dx || dy) && inked(x + dx, y + dy);
      if (edge) frame.blend(x0 + 1 + x, y0 + 1 + y, white, ring);
    }
  }
}

}  // namespace p64::widgets::notice

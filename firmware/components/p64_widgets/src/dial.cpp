#include "dial.hpp"

#include <algorithm>
#include <cstdlib>

#include "sprite.hpp"

namespace p64::widgets::themed::dial {

int sin_q(int tenths) { return assets::kSinQ14[((tenths % 3600) + 3600) % 3600]; }
int cos_q(int tenths) { return sin_q(tenths + 900); }

int profile_width(const Profile &p, int t) {
  if (t < p.points[0].t || t > p.points[p.count - 1].t) return -1;
  for (int i = 0; i + 1 < p.count; ++i) {
    const Point &a = p.points[i], &b = p.points[i + 1];
    if (t <= b.t) return a.w + floor_div((b.w - a.w) * (t - a.t), b.t - a.t);
  }
  return p.points[p.count - 1].w;
}

void draw_shape(gfx::Frame &frame, int cx2, int cy2, int angle10, const Profile &p, const Shade &shade, uint8_t shadow) {
  const int ux = sin_q(angle10), uy = -cos_q(angle10);  // along the axis
  const int vx = cos_q(angle10), vy = sin_q(angle10);   // across it
  // Only the pixels around the shape are tried: the box of its two ends, widened by its
  // greatest half width and two pixels (60 markers a frame at ten frames a second cost
  // the station clock a third of core 1 when each scanned the whole dial).
  int w_max = 0;
  for (int i = 0; i < p.count; ++i) w_max = std::max(w_max, p.points[i].w);
  const int t0 = p.points[0].t, t1 = p.points[p.count - 1].t;
  const int pad = w_max + 128;
  const int ax = cx2 * 32 + ((ux * t0) >> 14), bx = cx2 * 32 + ((ux * t1) >> 14);  // 64ths of a pixel
  const int ay = cy2 * 32 + ((uy * t0) >> 14), by = cy2 * 32 + ((uy * t1) >> 14);
  const int x_lo = floor_div(std::min(ax, bx) - pad, 64), x_hi = floor_div(std::max(ax, bx) + pad, 64) + 1;
  const int y_lo = floor_div(std::min(ay, by) - pad, 64), y_hi = floor_div(std::max(ay, by) + pad, 64) + 1;
  // Whether the pixel is inked and, if so, where across the shape it is.
  const auto inked = [&](int x, int y, int &s, int &w) {
    const int dx = 2 * x - cx2, dy = 2 * y - cy2;  // half pixels
    w = profile_width(p, (dx * ux + dy * uy) >> 9);  // Q14 half pixels to 64ths of a pixel
    if (w < 0) return false;
    s = (dx * vx + dy * vy) >> 9;
    return std::abs(s) <= w;
  };
  int s = 0, w = 0;
  if (shadow) {
    for (int y = y_lo + 1; y <= y_hi + 1; ++y)
      for (int x = x_lo + 1; x <= x_hi + 1; ++x)
        if (inked(x - 1, y - 1, s, w) && !inked(x, y, s, w)) frame.blend(x, y, gfx::kBlack, shadow);
  }
  for (int y = y_lo; y <= y_hi; ++y)
    for (int x = x_lo; x <= x_hi; ++x)
      if (inked(x, y, s, w)) frame.set(x, y, shade.at(s, w));
}

void polar_px(int cx2, int cy2, int angle10, int r2, int &x, int &y) {
  constexpr int kQ = assets::kSinQ;
  x = (cx2 * (1 << kQ) + sin_q(angle10) * r2 + (1 << kQ)) >> (kQ + 1);
  y = (cy2 * (1 << kQ) - cos_q(angle10) * r2 + (1 << kQ)) >> (kQ + 1);
}

void polar_half(int cx2, int cy2, int angle10, int r2, int &x2, int &y2) {
  x2 = cx2 + q_round(sin_q(angle10) * r2);
  y2 = cy2 + q_round(-cos_q(angle10) * r2);
}

void thin_hand(gfx::Frame &frame, int cx2, int cy2, int angle10, int length2, int tail2, gfx::Rgb colour) {
  int x0, y0, x1, y1;
  polar_px(cx2, cy2, angle10, -tail2, x0, y0);
  polar_px(cx2, cy2, angle10, length2, x1, y1);
  sprite::line(frame, x0, y0, x1, y1, colour);
}

}  // namespace p64::widgets::themed::dial

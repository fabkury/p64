#include "p64/gfx/frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace p64::gfx {

void Frame::clear(Rgb c) {
  if (c.r == c.g && c.g == c.b) {
    std::memset(px_, c.r, sizeof(px_));
    return;
  }
  for (int i = 0; i < kPanelWidth * kPanelHeight; ++i) {
    px_[i * 3 + 0] = c.r;
    px_[i * 3 + 1] = c.g;
    px_[i * 3 + 2] = c.b;
  }
}

void Frame::set(int x, int y, Rgb c) {
  if (x < 0 || y < 0 || x >= kPanelWidth || y >= kPanelHeight) return;
  uint8_t *p = &px_[(y * kPanelWidth + x) * 3];
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
}

Rgb Frame::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= kPanelWidth || y >= kPanelHeight) return kBlack;
  const uint8_t *p = &px_[(y * kPanelWidth + x) * 3];
  return Rgb{p[0], p[1], p[2]};
}

void Frame::fill_rect(int x, int y, int w, int h, Rgb c) {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, kPanelWidth);
  const int y1 = std::min(y + h, kPanelHeight);
  for (int yy = y0; yy < y1; ++yy) {
    uint8_t *p = &px_[(yy * kPanelWidth + x0) * 3];
    for (int xx = x0; xx < x1; ++xx, p += 3) {
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
    }
  }
}

void Frame::blend(int x, int y, Rgb c, uint8_t alpha) {
  if (x < 0 || y < 0 || x >= kPanelWidth || y >= kPanelHeight) return;
  if (alpha == 255) {
    set(x, y, c);
    return;
  }
  if (alpha == 0) return;
  uint8_t *p = &px_[(y * kPanelWidth + x) * 3];
  const unsigned a = alpha, ia = 255u - alpha;
  p[0] = static_cast<uint8_t>((p[0] * ia + c.r * a + 127u) / 255u);
  p[1] = static_cast<uint8_t>((p[1] * ia + c.g * a + 127u) / 255u);
  p[2] = static_cast<uint8_t>((p[2] * ia + c.b * a + 127u) / 255u);
}

void Frame::blend_rect(int x, int y, int w, int h, Rgb c, uint8_t alpha) {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, kPanelWidth);
  const int y1 = std::min(y + h, kPanelHeight);
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) blend(xx, yy, c, alpha);
  }
}

void Frame::fill_disc(float cx, float cy, float radius, Rgb c) {
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
  const int x1 = std::min(kPanelWidth - 1, static_cast<int>(std::ceil(cx + radius)));
  const int y1 = std::min(kPanelHeight - 1, static_cast<int>(std::ceil(cy + radius)));
  const float r2 = radius * radius;
  for (int y = y0; y <= y1; ++y) {
    const float dy = static_cast<float>(y) + 0.5f - cy;
    for (int x = x0; x <= x1; ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx;
      if (dx * dx + dy * dy <= r2) set(x, y, c);
    }
  }
}

void Frame::copy_from(const Frame &other) { std::memcpy(px_, other.px_, sizeof(px_)); }

void ChannelLut::set_gains(unsigned r_pct, unsigned g_pct, unsigned b_pct) {
  auto fill = [](uint8_t *lut, unsigned pct) {
    pct = std::min(pct, 100u);
    for (unsigned v = 0; v < 256; ++v) lut[v] = static_cast<uint8_t>((v * pct + 50u) / 100u);
  };
  fill(r, r_pct);
  fill(g, g_pct);
  fill(b, b_pct);
}

void rotate_copy(const Frame &src, uint8_t *dst, Rotation rotation, bool mirror, const ChannelLut &lut) {
  static_assert(kPanelWidth == kPanelHeight, "rotation by 90/270 degrees needs a square panel");
  constexpr int W = kPanelWidth, H = kPanelHeight;
  const uint8_t *s = src.data();
  // Physical (px, py) takes the logical pixel that lands there after a clockwise turn:
  //   90:  logical (x, y) -> physical (W-1-y, x)   so physical (px, py) <- logical (py, W-1-px)
  //   180: logical (x, y) -> physical (W-1-x, H-1-y)
  //   270: logical (x, y) -> physical (y, H-1-x)   so physical (px, py) <- logical (H-1-py, px)
  // Every case is linear, lx = ax + bx*px + cx*py and ly = ay + by*px + cy*py, so the source
  // is walked with one constant step per row and the inner loop has no branch.
  int ax = 0, bx = 0, cx = 0, ay = 0, by = 0, cy = 0;
  switch (rotation) {
    case Rotation::R90:
      cx = 1;
      ay = W - 1;
      by = -1;
      break;
    case Rotation::R180:
      ax = W - 1;
      bx = -1;
      ay = H - 1;
      cy = -1;
      break;
    case Rotation::R270:
      ax = H - 1;
      cx = -1;
      by = 1;
      break;
    default:
      bx = 1;
      cy = 1;
      break;
  }
  // The mirror flips the logical picture left-right before the turn: logical x -> W-1-x.
  if (mirror) {
    ax = W - 1 - ax;
    bx = -bx;
    cx = -cx;
  }
  const int step = (by * W + bx) * 3;
  uint8_t *d = dst;
  for (int py = 0; py < H; ++py) {
    int i = ((ay + cy * py) * W + ax + cx * py) * 3;
    for (int px = 0; px < W; ++px, d += 3, i += step) {
      d[0] = lut.r[s[i]];
      d[1] = lut.g[s[i + 1]];
      d[2] = lut.b[s[i + 2]];
    }
  }
}

}  // namespace p64::gfx

#include "sprite.hpp"

#include <cstdlib>

namespace p64::widgets::sprite {

View view(const Sprite &s) { return {s.rgba, s.w, s.w, s.h}; }

View cell(const Sprite &sheet, int count, int i) {
  const int w = (sheet.w + 1) / count - 1;
  return {sheet.rgba + i * (w + 1) * 4, sheet.w, w, sheet.h};
}

View sub(const View &v, int x0, int y0, int w, int h) { return {v.at(x0, y0), v.stride, w, h}; }

void blit(gfx::Frame &frame, const View &v, int x, int y) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) frame.set(x + xx, y + yy, v.colour(xx, yy));
}

void stamp(gfx::Frame &frame, const View &v, int x, int y, gfx::Rgb colour) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) frame.set(x + xx, y + yy, colour);
}

void stamp_halo(gfx::Frame &frame, const View &v, int x, int y, gfx::Rgb colour) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) frame.fill_rect(x + xx - 1, y + yy - 1, 3, 3, colour);
}

void line(gfx::Frame &frame, int x0, int y0, int x1, int y1, gfx::Rgb colour) {
  const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    frame.set(x0, y0, colour);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

}  // namespace p64::widgets::sprite

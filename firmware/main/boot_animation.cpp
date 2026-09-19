#include "boot_animation.hpp"

#include <algorithm>
#include <cmath>

namespace p64 {
namespace {

using gfx::Frame;
using gfx::Rgb;

uint8_t to_byte(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// h in turns (wraps), s and v in [0, 1].
Rgb hsv(float h, float s, float v) {
  const float hh = (h - std::floor(h)) * 6.0f;
  const int sector = static_cast<int>(hh) % 6;
  const float f = hh - std::floor(hh);
  const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
  float r, g, b;
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return Rgb{to_byte(r), to_byte(g), to_byte(b)};
}

}  // namespace

// Three rings expand from the centre, each a little later and a little bluer than the
// last, fading as they grow; the picture is black again by the end so the first artwork
// lands on a dark panel.
bool BootAnimation::render(Frame &frame, uint32_t t_ms, uint32_t duration_ms) const {
  frame.clear();
  if (duration_ms == 0) return false;
  const float u = static_cast<float>(t_ms) / static_cast<float>(duration_ms);  // 0..1 over the animation
  if (u >= 1.0f) return false;
  const float cx = Frame::width() * 0.5f, cy = Frame::height() * 0.5f;
  const float max_r = std::sqrt(cx * cx + cy * cy);
  for (int ring = 0; ring < 3; ++ring) {
    const float start = 0.08f * ring;
    const float span = 0.75f;
    const float w = (u - start) / span;  // ring progress 0..1
    if (w <= 0.0f || w >= 1.0f) continue;
    const float radius = w * max_r;
    const float thickness = 2.5f + 3.0f * w;
    const float fade = (1.0f - w) * (1.0f - w);
    const float hue = 0.58f + 0.12f * ring - 0.25f * w;  // blue-violet drifting towards cyan
    for (int y = 0; y < Frame::height(); ++y) {
      for (int x = 0; x < Frame::width(); ++x) {
        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        const float d = std::sqrt(dx * dx + dy * dy) - radius;
        const float a = 1.0f - std::fabs(d) / thickness;
        if (a <= 0.0f) continue;
        const Rgb c = hsv(hue, 0.85f, a * fade);
        const Rgb prev = frame.get(x, y);
        frame.set(x, y, Rgb{static_cast<uint8_t>(std::min(255, prev.r + c.r)),
                            static_cast<uint8_t>(std::min(255, prev.g + c.g)),
                            static_cast<uint8_t>(std::min(255, prev.b + c.b))});
      }
    }
  }
  return true;
}

void IdlePattern::render(Frame &frame, uint32_t t_ms) const {
  const float t = static_cast<float>(t_ms) / 1000.0f;
  const float breath = 0.06f + 0.05f * (0.5f + 0.5f * std::sin(t * 0.8f));
  for (int y = 0; y < Frame::height(); ++y) {
    for (int x = 0; x < Frame::width(); ++x) {
      const float h = 0.6f + 0.15f * (x + y) / static_cast<float>(Frame::width() + Frame::height()) + t * 0.01f;
      frame.set(x, y, hsv(h, 0.7f, breath));
    }
  }
}

}  // namespace p64

#include "scenes/hue_wheel.hpp"

#include <algorithm>
#include <cmath>

namespace p64 {
namespace {

constexpr float kPi = 3.14159265358979f;

uint8_t to_byte(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// h in [0, 1), s and v in [0, 1].
Rgb hsv_to_rgb(float h, float s, float v) {
  const float hh = (h - std::floor(h)) * 6.0f;
  const int sector = static_cast<int>(hh) % 6;
  const float f = hh - std::floor(hh);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
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

void HueWheelScene::build_tables() {
  const float cx = static_cast<float>(kWidth) / 2.0f;
  const float cy = static_cast<float>(kHeight) / 2.0f;
  const float max_dist = std::max(cx, cy) - 0.5f;  // centre of the outermost pixel ring
  for (int y = 0; y < kHeight; ++y) {
    const float dy = static_cast<float>(y) + 0.5f - cy;
    for (int x = 0; x < kWidth; ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx;
      const float angle = std::atan2(dy, dx);  // -pi..pi
      const float chebyshev = std::max(std::fabs(dx), std::fabs(dy));  // 0.5..max_dist
      const int i = y * kWidth + x;
      hue_[i] = static_cast<uint8_t>(std::lround((angle + kPi) / (2.0f * kPi) * 255.0f));
      saturation_[i] = to_byte((chebyshev - 0.5f) / (max_dist - 0.5f));
    }
  }
  tables_ready_ = true;
}

void HueWheelScene::enter(Display &display, Frame &frame) {
  if (!tables_ready_) build_tables();
  display.set_brightness(max_brightness());
  render(display, frame, 0, 0.0f);
  display.present(frame);
}

bool HueWheelScene::render(Display &, Frame &frame, uint32_t t_ms, float) {
  // One full turn over the phase.
  const float offset = static_cast<float>(t_ms) / static_cast<float>(duration_ms());
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const int i = y * kWidth + x;
      const float h = static_cast<float>(hue_[i]) / 256.0f + offset;
      const float s = static_cast<float>(saturation_[i]) / 255.0f;
      frame.set(x, y, hsv_to_rgb(h, s, 1.0f));
    }
  }
  return true;
}

}  // namespace p64

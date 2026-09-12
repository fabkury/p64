#include "scenes/hue_wheel.hpp"

#include <algorithm>
#include <cmath>

#include "color.hpp"

namespace p64 {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint32_t kTurnMs = 20 * 1000;  // one full rotation

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
  paint(frame, 0);
  display.present(frame);
}

bool HueWheelScene::render(Display &, Frame &frame, const FrameInfo &info) {
  paint(frame, info.t_ms);
  return true;
}

void HueWheelScene::paint(Frame &frame, uint32_t t_ms) const {
  const float offset = static_cast<float>(t_ms % kTurnMs) / static_cast<float>(kTurnMs);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const int i = y * kWidth + x;
      const float h = static_cast<float>(hue_[i]) / 256.0f + offset;
      const float s = static_cast<float>(saturation_[i]) / 255.0f;
      frame.set(x, y, hsv_to_rgb(h, s, 1.0f));
    }
  }
}

}  // namespace p64

#include "scenes/fade.hpp"

#include <algorithm>
#include <cmath>

namespace p64 {
namespace {

// Inverse of the CIE 1931 lightness function: perceived lightness L (0..1) to
// relative luminance Y (0..1). Panel brightness is linear in luminance.
float cie_luminance(float lightness) {
  if (lightness <= 0.08f) return lightness / 9.033f;
  const float t = (lightness + 0.16f) / 1.16f;
  return t * t * t;
}

}  // namespace

void FadeScene::enter(Display &display, Frame &frame) {
  last_ = -1;
  frame.clear(kWhite);
  display.set_brightness(max_brightness());
  display.present(frame);
}

bool FadeScene::render(Display &display, Frame &, uint32_t t_ms, float) {
  const float half = static_cast<float>(duration_ms()) / 2.0f;
  const float t = static_cast<float>(t_ms);
  // Lightness goes 1 -> 0 over the first half and 0 -> 1 over the second.
  float lightness = (t < half) ? 1.0f - t / half : (t - half) / half;
  lightness = std::clamp(lightness, 0.0f, 1.0f);

  const float luminance = (curve_ == Curve::Linear) ? lightness : cie_luminance(lightness);
  const int value = static_cast<int>(std::lround(luminance * max_brightness()));
  if (value != last_) {
    last_ = value;
    display.set_brightness(static_cast<uint8_t>(value));
  }
  return false;  // the frame itself never changes
}

}  // namespace p64

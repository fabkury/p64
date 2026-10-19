#include "p64/inputs/knob_rules.hpp"

namespace p64::inputs {

uint8_t brightness_after(uint8_t current, int32_t detents) {
  // The brightness number is perceived lightness (light_curve.hpp, spec 3.2), so a fixed
  // step is an even step to the eye: kDetentStep of 254 from 1 to 255, 37 detents each
  // way (the last one shorter), every one visible.
  long value = (current < 1 ? 1 : current) + static_cast<long>(detents) * kDetentStep;
  if (value > 255) value = 255;
  if (value < 1) value = 1;
  return static_cast<uint8_t>(value);
}

uint8_t notice_alpha(int64_t elapsed_us) {
  if (elapsed_us < 0) return 0;
  if (elapsed_us < kNoticeHoldUs) return 255;
  const int64_t into = elapsed_us - kNoticeHoldUs;
  if (into >= kNoticeFadeUs) return 0;
  // Step i (0-based) of the fade: 255 * (steps - i) / (steps + 1): 204, 153, 102, 51.
  const int i = static_cast<int>(into * kNoticeFadeSteps / kNoticeFadeUs);
  return static_cast<uint8_t>(255 * (kNoticeFadeSteps - i) / (kNoticeFadeSteps + 1));
}

}  // namespace p64::inputs

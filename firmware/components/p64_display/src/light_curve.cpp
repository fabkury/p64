#include "p64/display/light_curve.hpp"

#include <cmath>

namespace p64::display::light {

double lightness(uint8_t brightness) {
  if (brightness == 0) return 0.0;
  const double l1 = std::cbrt(kFloorLight);
  return l1 + (brightness - 1) / 254.0 * (1.0 - l1);
}

uint32_t light_q16(uint8_t brightness) {
  if (brightness == 0) return 0;
  if (brightness == 255) return 65536u;
  const double l = lightness(brightness);
  const double share = l * l * l;
  const uint32_t q16 = static_cast<uint32_t>(share * 65536.0 + 0.5);
  return q16 < 1 ? 1 : q16;
}

}  // namespace p64::display::light

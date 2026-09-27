// Host unit tests: the brightness scale (p64_display light_curve.hpp, spec 3.2): the
// user's 1..255 as perceived lightness, and the share of full light each value asks of
// the driver's light plan.
#include "common.hpp"
#include "p64/display/light_curve.hpp"

#include <cmath>

namespace {

using p64::display::light::kFloorLight;
using p64::display::light::light_q16;
using p64::display::light::lightness;

TEST_CASE("light: the ends of the scale are the floor and the full light, 0 is dark") {
  CHECK_EQ(light_q16(0), 0u);
  CHECK_EQ(light_q16(255), 65536u);
  // Brightness 1: a sixteenth of the driver floor, about 0.4 % of full (263 of 65536).
  const uint32_t one = light_q16(1);
  CHECK(one >= 260u);
  CHECK(one <= 266u);
  CHECK(std::fabs(one / 65536.0 - kFloorLight) < 1e-5);
  CHECK_EQ(lightness(0), 0.0);
  CHECK(std::fabs(lightness(255) - 1.0) < 1e-12);
  CHECK(std::fabs(lightness(1) - std::cbrt(kFloorLight)) < 1e-12);
}

TEST_CASE("light: the scale is strictly increasing, in lightness and in light") {
  for (int v = 2; v <= 255; ++v) {
    CHECK_MESSAGE(light_q16(static_cast<uint8_t>(v)) > light_q16(static_cast<uint8_t>(v - 1)), "brightness ", v);
    CHECK(lightness(static_cast<uint8_t>(v)) > lightness(static_cast<uint8_t>(v - 1)));
  }
}

TEST_CASE("light: even steps of the number are even steps of lightness (the knob's premise)") {
  const double step = lightness(2) - lightness(1);
  for (int v = 2; v <= 255; ++v) {
    CHECK(std::fabs((lightness(static_cast<uint8_t>(v)) - lightness(static_cast<uint8_t>(v - 1))) - step) < 1e-9);
  }
}

TEST_CASE("light: the light is the cube of the lightness; 128 is about a fifth of the light") {
  for (int v = 1; v <= 255; ++v) {
    const double l = lightness(static_cast<uint8_t>(v));
    CHECK(std::fabs(light_q16(static_cast<uint8_t>(v)) / 65536.0 - l * l * l) < 1.0 / 65536.0);
  }
  const double half = light_q16(128) / 65536.0;
  CHECK(half > 0.18);
  CHECK(half < 0.21);
}

}  // namespace

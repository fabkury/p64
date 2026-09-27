// Host unit tests: the panel's bit-plane arithmetic (esp-hub75 p64 patch, p64_bcm.h):
// the output-enable windows of each refresh profile and the LUT fitted to them.
#include "common.hpp"
#include "p64_bcm.h"

#include <cmath>

namespace {

namespace bcm = hub75::p64bcm;

constexpr int kMaxPixels = 63;  // a 64-clock row minus one clock of latch blanking (sdkconfig)
constexpr uint32_t kIdealMax = 1023;

// A gamma 2.2 table over 0..1023, the shape of the driver's (its exact rounding differs a
// little, which moves a code here and there, not the invariants tested below).
struct Gamma {
  uint16_t v[256];
  Gamma() {
    for (int i = 0; i < 256; ++i) v[i] = static_cast<uint16_t>(std::lround(std::pow(i / 255.0, 2.2) * kIdealMax));
  }
};
const Gamma kGamma;

struct Profile {
  int windows[16] = {};
  uint32_t weights[16] = {};
  uint16_t lut[256] = {};
  unsigned distinct = 0;
};

Profile fit(int planes, int transition, int brightness) {
  Profile p;
  bcm::plane_windows(kMaxPixels, brightness, planes, transition, p.windows);
  for (int bit = 0; bit < planes; ++bit) p.weights[bit] = p.windows[bit] * bcm::plane_reps(bit, transition);
  p.distinct = bcm::fit_lut(kGamma.v, kIdealMax, p.weights, planes, p.lut);
  return p;
}

// Every lit plane weighs at least all the planes below it (a blanked plane, weight 0, is
// allowed: the fit leaves it out).
bool superincreasing(const Profile &p, int planes) {
  uint32_t below = 0;
  for (int bit = 0; bit < planes; ++bit) {
    if (p.weights[bit] != 0 && p.weights[bit] < below) return false;
    below += p.weights[bit];
  }
  return true;
}

// The light of each LUT entry never decreases along the inputs.
bool light_monotonic(const Profile &p, int planes) {
  uint32_t last = 0;
  for (int i = 0; i < 256; ++i) {
    uint32_t w = 0;
    for (int bit = 0; bit < planes; ++bit) {
      if (p.lut[i] & (1u << bit)) w += p.weights[bit];
    }
    if (w < last) return false;
    last = w;
  }
  return true;
}

bool monotonic(const Profile &p) {
  for (int i = 1; i < 256; ++i) {
    if (p.lut[i] < p.lut[i - 1]) return false;
  }
  return true;
}

TEST_CASE("bcm: the Photo profile's windows are the ones the device logs (8 planes, transition 4)") {
  const Profile p = fit(8, 4, 255);
  const int want[8] = {1, 3, 7, 15, 31, 62, 62, 62};
  for (int bit = 0; bit < 8; ++bit) CHECK_EQ(p.windows[bit], want[bit]);
  CHECK(superincreasing(p, 8));
  CHECK(monotonic(p));
  CHECK(p.distinct >= 170);  // 179 on the device with the driver's own gamma table
}

TEST_CASE("bcm: the Quality profile (10 planes, transition 4) keeps more levels than Photo") {
  const Profile q = fit(10, 4, 255);
  const Profile ph = fit(8, 4, 255);
  CHECK(superincreasing(q, 10));
  CHECK(monotonic(q));
  CHECK(q.distinct > ph.distinct);
  CHECK_EQ(q.lut[0], 0);
  CHECK_EQ(q.lut[255], (1 << 10) - 1);  // full white is every plane
}

TEST_CASE("bcm: ten planes at transition bit 6 no longer collapse to three levels (2026-09-20)") {
  // Planes 0, 1 and 2 all floored to one clock; the fit's walk then stuck at code 3 and
  // every input above 13 came out as code 3. The low plane that would break the order is
  // blanked now.
  const Profile p = fit(10, 6, 255);
  CHECK(superincreasing(p, 10));
  CHECK(light_monotonic(p, 10));
  CHECK(p.distinct > 100);
  CHECK(p.lut[128] > 3);
  CHECK(p.lut[255] > p.lut[128]);
  CHECK_EQ(p.lut[255] & (1u << 2), 0u);  // the blanked plane is never lit
}

TEST_CASE("bcm: every profile at every brightness keeps the weights superincreasing and the LUT monotonic") {
  int profiles = 0;
  for (int planes = 6; planes <= 10; ++planes) {
    for (int transition = 0; transition < planes; ++transition) {
      for (int b = 1; b <= 255; ++b) {
        const Profile p = fit(planes, transition, b);
        CHECK_MESSAGE(superincreasing(p, planes), "planes ", planes, " transition ", transition, " brightness ", b);
        CHECK_MESSAGE(light_monotonic(p, planes), "planes ", planes, " transition ", transition, " brightness ", b);
        for (int bit = 0; bit < planes; ++bit) CHECK(p.windows[bit] <= kMaxPixels - 1);  // the blanking margin
        ++profiles;
      }
    }
  }
  CHECK_EQ(profiles, (6 + 7 + 8 + 9 + 10) * 255);
}

TEST_CASE("bcm: the profiles in use lose no level to the blanking (229 and 179 codes at full brightness)") {
  // No plane is blanked in Quality or Photo at any brightness, so the walk over lit
  // planes is the walk over every code, and the counts are those of the device's log.
  CHECK_EQ(fit(10, 4, 255).distinct, 229u);
  CHECK_EQ(fit(8, 4, 255).distinct, 179u);
  for (int b = 1; b <= 255; ++b) {
    for (const Profile &p : {fit(10, 4, b), fit(8, 4, b)}) {
      // Dark planes only at the bottom (the brightness costs the low planes first).
      bool lit_seen = false, blanked_above_lit = false;
      for (int bit = 0; bit < 10; ++bit) {
        if (p.weights[bit]) lit_seen = true;
        else if (lit_seen && bit < (p.lut[255] >= 512 ? 10 : 8)) blanked_above_lit = true;
      }
      CHECK_MESSAGE(!blanked_above_lit, "brightness ", b);
      CHECK(monotonic(p));
    }
  }
}

TEST_CASE("bcm: brightness 0 is dark, and the LUT fit says so") {
  const Profile p = fit(10, 4, 0);
  for (int bit = 0; bit < 10; ++bit) CHECK_EQ(p.windows[bit], 0);
  CHECK_EQ(p.distinct, 0u);
}

// The light plan (spec 3.2, 2026-09-27): a share of the full light becomes an
// output-enable level plus a LUT scale. The driver's floor on this panel is 17.
constexpr int kFloor = 17;

// The light a plan emits at input 255 of a fitted LUT, in clocks per frame.
uint32_t top_light(const bcm::LightPlan &plan, int planes, int transition) {
  int windows[16] = {};
  uint32_t weights[16] = {};
  uint16_t lut[256] = {};
  bcm::plane_windows(kMaxPixels, plan.effective, planes, transition, windows);
  for (int bit = 0; bit < planes; ++bit) weights[bit] = windows[bit] * bcm::plane_reps(bit, transition);
  bcm::fit_lut(kGamma.v, kIdealMax, weights, planes, lut, plan.scale_q16);
  uint32_t w = 0;
  for (int bit = 0; bit < planes; ++bit) {
    if (lut[255] & (1u << bit)) w += weights[bit];
  }
  return w;
}

TEST_CASE("bcm: the Quality profile's full light is the 1979 clocks the device logs") {
  CHECK_EQ(bcm::weight_total(kMaxPixels, 255, 10, 4), 1979u);
  CHECK_EQ(bcm::weight_total(kMaxPixels, kFloor, 10, 4), 127u);  // the driver floor: 6.4 % of full
  CHECK_EQ(bcm::weight_total(kMaxPixels, 0, 10, 4), 0u);
}

TEST_CASE("bcm: the light plan lands every request within a clock and never drops below the floor level") {
  uint32_t last = 0;
  for (uint32_t light = 1; light <= 65536u; light += 37) {
    const bcm::LightPlan plan = bcm::plan_light(kMaxPixels, 10, 4, kFloor, light);
    CHECK(plan.effective >= kFloor);
    CHECK(plan.effective <= 255);
    CHECK(plan.scale_q16 >= 1u);
    CHECK(plan.scale_q16 <= 65536u);
    const double want = 1979.0 * light / 65536.0;
    const double got = static_cast<double>(plan.weight) * plan.scale_q16 / 65536.0;
    CHECK_MESSAGE(std::fabs(got - want) <= 1.0, "light ", light, " want ", want, " got ", got);
    // The level's own light reaches the request (the scale only ever dims).
    const bool reaches = static_cast<double>(plan.weight) + 0.5 >= want || plan.effective == 255;
    CHECK(reaches);
    CHECK(plan.weight >= last);  // levels only rise with the request
    last = plan.weight;
  }
  const bcm::LightPlan full = bcm::plan_light(kMaxPixels, 10, 4, kFloor, 65536u);
  CHECK_EQ(full.effective, 255);
  CHECK_EQ(full.scale_q16, 65536u);
  CHECK_EQ(bcm::plan_light(kMaxPixels, 10, 4, kFloor, 0).effective, 0);
}

TEST_CASE("bcm: above the floor the LUT scale stays near one (the levels are at most 25 % apart)") {
  for (uint32_t light = 4300; light <= 65536u; light += 101) {  // 6.6 % and up
    const bcm::LightPlan plan = bcm::plan_light(kMaxPixels, 10, 4, kFloor, light);
    CHECK_MESSAGE(plan.scale_q16 >= 65536u * 3 / 4, "light ", light, " scale ", plan.scale_q16);
  }
}

TEST_CASE("bcm: below the floor the level stays at the floor and the LUT scale dims, a bit of depth per halving") {
  // A sixteenth of the floor: the new brightness 1 (spec 3.2), about 0.4 % of full.
  const uint32_t sixteenth = static_cast<uint32_t>(65536.0 * 127 / 1979 / 16 + 0.5);
  const bcm::LightPlan plan = bcm::plan_light(kMaxPixels, 10, 4, kFloor, sixteenth);
  CHECK_EQ(plan.effective, kFloor);
  CHECK_EQ(plan.weight, 127u);
  CHECK(plan.scale_q16 > 65536u / 17);
  CHECK(plan.scale_q16 < 65536u / 15);
  const uint32_t top = top_light(plan, 10, 4);
  CHECK(top >= 7u);
  CHECK(top <= 9u);
  // The fitted LUT is still monotonic in light and has a handful of distinct codes.
  int windows[16] = {};
  uint32_t weights[16] = {};
  uint16_t lut[256] = {};
  bcm::plane_windows(kMaxPixels, plan.effective, 10, 4, windows);
  for (int bit = 0; bit < 10; ++bit) weights[bit] = windows[bit] * bcm::plane_reps(bit, 4);
  const unsigned distinct = bcm::fit_lut(kGamma.v, kIdealMax, weights, 10, lut, plan.scale_q16);
  CHECK(distinct >= 6u);
  CHECK(distinct <= 10u);
  uint32_t last = 0;
  for (int i = 0; i < 256; ++i) {
    uint32_t w = 0;
    for (int bit = 0; bit < 10; ++bit) {
      if (lut[i] & (1u << bit)) w += weights[bit];
    }
    CHECK(w >= last);
    last = w;
  }
  CHECK_EQ(lut[0], 0u);
  // Half the floor keeps twice the codes of a sixteenth, give or take the rounding.
  const bcm::LightPlan half = bcm::plan_light(kMaxPixels, 10, 4, kFloor, sixteenth * 8);
  CHECK_EQ(half.effective, kFloor);
  CHECK(top_light(half, 10, 4) >= 60u);
}

TEST_CASE("bcm: the Photo profile plans the same share of its own full light") {
  const uint32_t full8 = bcm::weight_total(kMaxPixels, 255, 8, 4);
  CHECK(full8 < 1979u);
  const bcm::LightPlan plan = bcm::plan_light(kMaxPixels, 8, 4, kFloor, 32768u);
  const double got = static_cast<double>(plan.weight) * plan.scale_q16 / 65536.0;
  CHECK(std::fabs(got - full8 / 2.0) <= 1.0);
}

TEST_CASE("bcm: a scaled fit at full level is the unscaled fit's light times the scale") {
  const Profile p = fit(10, 4, 255);
  uint16_t lut[256] = {};
  bcm::fit_lut(kGamma.v, kIdealMax, p.weights, 10, lut, 32768u);
  uint32_t w = 0;
  for (int bit = 0; bit < 10; ++bit) {
    if (lut[255] & (1u << bit)) w += p.weights[bit];
  }
  CHECK(w >= 1979u / 2 - 4);  // the nearest code: the codes are a few clocks apart
  CHECK(w <= 1979u / 2 + 4);
  for (int i = 1; i < 256; ++i) CHECK(lut[i] >= lut[i - 1]);
}

}  // namespace

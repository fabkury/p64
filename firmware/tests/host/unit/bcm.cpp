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

}  // namespace

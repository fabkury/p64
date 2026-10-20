// Host tests of the intro animations (p101): the registry, the embedded logo layers
// against the PNG files they were baked from (the trim keeps every pixel in place), and
// the contract every animation honours: the background alone at t = 0, the finished logo
// from the hold on with an invisible handoff, the same bytes for the same seed and time,
// every frame of a 3 s window at 50 fps drawn without a fault, and motion in between.
#include <chrono>
#include <set>

#include "common.hpp"
#include "intro_common.hpp"
#include "p64/intro/intro.hpp"
#include "p64/decode/png_decoder.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;

bool same(const Frame &a, const Frame &b) { return std::memcmp(a.data(), b.data(), Frame::bytes()) == 0; }

std::vector<uint8_t> read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// docs/images/logo/v2 relative to this file (tests/host/unit).
std::string logo_dir() {
  std::string here = __FILE__;
  const size_t cut = here.find_last_of("/\\");
  here = here.substr(0, cut);
  return here + "/../../../../docs/images/logo/v2/";
}

// Composites a 64x64 RGBA PNG over `frame` with the firmware's 0/255 alpha rule (alpha
// 128 and up is ink). The decoder flattens over a colour; the two-background trick of
// picture.cpp recovers which pixels are transparent.
bool composite_png(const std::string &path, Frame &frame) {
  const std::vector<uint8_t> bytes = read_file(path);
  if (bytes.empty()) return false;
  p64::decode::PngDecoder over_black, over_white;
  uint32_t delay = 0;
  if (!over_black.open(bytes.data(), bytes.size(), p64::gfx::kBlack) || !over_black.next(delay)) return false;
  if (!over_white.open(bytes.data(), bytes.size(), p64::gfx::kWhite) || !over_white.next(delay)) return false;
  if (over_black.info().width != 64 || over_black.info().height != 64) return false;
  const uint8_t *b = over_black.canvas(), *w = over_white.canvas();
  for (int i = 0; i < 64 * 64; ++i) {
    const int through = w[i * 3] - b[i * 3];  // 255 - alpha
    if (through >= 128) continue;
    frame.set(i % 64, i / 64, Rgb{b[i * 3], b[i * 3 + 1], b[i * 3 + 2]});
  }
  return true;
}

uint8_t g_scratch[p64::intro::kScratchBytes];
const p64::intro::Context kCtx{0x1234567u, Rgb{7, 9, 11}, g_scratch};

}  // namespace

TEST_CASE("intro: the registry has 26 named animations found by name") {
  CHECK_EQ(p64::intro::count(), 26);
  std::set<std::string> names;
  for (int i = 0; i < p64::intro::count(); ++i) {
    const char *n = p64::intro::name(i);
    REQUIRE(n != nullptr);
    CHECK_EQ(p64::intro::find(n), i);
    for (const char *c = n; *c; ++c) {
      const bool ok = (*c >= 'a' && *c <= 'z') || *c == '_' || (*c >= '0' && *c <= '9');
      CHECK(ok);
    }
    names.insert(n);
  }
  CHECK_EQ(names.size(), static_cast<size_t>(p64::intro::count()));
  CHECK_EQ(p64::intro::find("no_such_animation"), -1);
  CHECK_EQ(p64::intro::find(nullptr), -1);
  CHECK(p64::intro::name(-1) == nullptr);
  CHECK(p64::intro::name(p64::intro::count()) == nullptr);
}

TEST_CASE("intro: the embedded layers reproduce the logo PNGs pixel for pixel, offsets kept") {
  Frame expected;
  expected.clear(kCtx.background);
  REQUIRE(composite_png(logo_dir() + "p64-logo-v2-L0.png", expected));
  REQUIRE(composite_png(logo_dir() + "p64-logo-v2-L1.png", expected));
  Frame got;
  p64::intro::finished(got, kCtx);
  CHECK(same(got, expected));
  // An out-of-range index draws the finished logo too (a safe default).
  Frame other;
  p64::intro::render(99, other, kCtx, 0.3f);
  CHECK(same(other, expected));
}

TEST_CASE("intro: every animation honours the contract") {
  Frame blank;
  blank.clear(kCtx.background);
  Frame done;
  p64::intro::finished(done, kCtx);
  constexpr float kWindowMs = 3000.0f;
  const int frames = static_cast<int>(kWindowMs / p64::intro::kFrameMs);
  for (int i = 0; i < p64::intro::count(); ++i) {
    CAPTURE(p64::intro::name(i));
    Frame f;
    p64::intro::render(i, f, kCtx, 0.0f);
    CHECK(same(f, blank));  // t = 0: the background alone
    // The last frame before the hold is the finished logo already (an invisible handoff).
    p64::intro::render(i, f, kCtx, p64::intro::kHoldFrom - 0.0005f);
    CHECK(same(f, done));
    p64::intro::render(i, f, kCtx, p64::intro::kHoldFrom);
    CHECK(same(f, done));
    p64::intro::render(i, f, kCtx, 1.0f);
    CHECK(same(f, done));
    // Deterministic for (seed, t).
    Frame g;
    p64::intro::render(i, f, kCtx, 0.37f);
    p64::intro::render(i, g, kCtx, 0.37f);
    CHECK(same(f, g));
    // Every frame of a 3 s window renders; the picture moves (distinct frames), and the
    // frame before the hold needs no more than the settled rule allows.
    std::set<std::string> distinct;
    int lit_frames = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < frames; ++k) {
      p64::intro::render(i, f, kCtx, k * p64::intro::kFrameMs / kWindowMs);
      distinct.insert(std::string(reinterpret_cast<const char *>(f.data()), Frame::bytes()));
      if (!same(f, blank)) ++lit_frames;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    CHECK(distinct.size() >= 30);
    CHECK(lit_frames >= frames / 2);
    CHECK(ms < 10.0);  // a host sanity bound; the device bench (diag/intro_bench) is the real budget
  }
}

TEST_CASE("intro: a seed changes the chance-driven animations but not the finish") {
  Frame done;
  p64::intro::finished(done, kCtx);
  p64::intro::Context other = kCtx;
  other.seed ^= 0xDEADBEEFu;
  const int dissolve = p64::intro::find("dissolve_duet");
  REQUIRE(dissolve >= 0);
  Frame a, b;
  p64::intro::render(dissolve, a, kCtx, 0.3f);
  p64::intro::render(dissolve, b, other, 0.3f);
  CHECK(!same(a, b));
  p64::intro::render(dissolve, b, other, 1.0f);
  CHECK(same(b, done));
  // A different background shows under the layers and in the finished logo.
  p64::intro::Context light = kCtx;
  light.background = Rgb{200, 200, 200};
  p64::intro::finished(a, light);
  CHECK(a.get(0, 0) == light.background);
  CHECK(a.get(32, 32) == done.get(32, 32));  // inside the panel: the picture, not the background
}

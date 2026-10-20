// The registry of intro animations (p101): the order here is the index the API and the
// settings name, and the catalogue in docs/intro-animations.md follows it. Each entry is
// defined in its own ia_<name>.cpp.
#include <cstring>

#include "intro_common.hpp"

namespace p64::intro {

extern const Anim kFadeDuet;
extern const Anim kSlideCross;
extern const Anim kBounceDrop;
extern const Anim kBandsCross;
extern const Anim kDissolveDuet;
extern const Anim kIrisTypewriter;
extern const Anim kVenetianCross;
extern const Anim kAssemble;
extern const Anim kGlitchSettle;
extern const Anim kChannelEmerge;
extern const Anim kHueSpin;
extern const Anim kSwirlOrbit;
extern const Anim kBlindsFlip;
extern const Anim kScanlineCross;
extern const Anim kStamp;
extern const Anim kShatterLetters;
extern const Anim kCurtainBanner;
extern const Anim kWaveSettle;
extern const Anim kPlasmaDuet;
extern const Anim kStarburstSparkle;
extern const Anim kTilesDiagonal;
extern const Anim kFlipCard;
extern const Anim kSpotlightSweep;
extern const Anim kCrtOn;
extern const Anim kFountain;
extern const Anim kTuneIn;

namespace {

const Anim *const kAnims[] = {
    &kFadeDuet,       &kSlideCross,  &kBounceDrop,     &kBandsCross,       &kDissolveDuet,  &kIrisTypewriter,
    &kVenetianCross,  &kAssemble,    &kGlitchSettle,   &kChannelEmerge,    &kHueSpin,       &kSwirlOrbit,
    &kBlindsFlip,     &kScanlineCross, &kStamp,        &kShatterLetters,   &kCurtainBanner, &kWaveSettle,
    &kPlasmaDuet,     &kStarburstSparkle, &kTilesDiagonal, &kFlipCard,      &kSpotlightSweep, &kCrtOn,
    &kFountain,       &kTuneIn,
};
constexpr int kCount = static_cast<int>(sizeof(kAnims) / sizeof(kAnims[0]));

}  // namespace

int count() { return kCount; }

const char *name(int index) { return index >= 0 && index < kCount ? kAnims[index]->name : nullptr; }

int find(const char *n) {
  if (!n) return -1;
  for (int i = 0; i < kCount; ++i)
    if (std::strcmp(kAnims[i]->name, n) == 0) return i;
  return -1;
}

void finished(Frame &frame, const Context &ctx) {
  fill(frame, ctx);
  draw(frame, L0());
  draw(frame, L1());
}

void render(int index, Frame &frame, const Context &ctx, float t) {
  if (index < 0 || index >= kCount) {
    finished(frame, ctx);
    return;
  }
  const float u = t / kHoldFrom;
  if (u <= 0.0f) {
    fill(frame, ctx);
    return;
  }
  if (u >= 1.0f) {
    finished(frame, ctx);
    return;
  }
  fill(frame, ctx);
  kAnims[index]->render(frame, ctx, u);
}

}  // namespace p64::intro

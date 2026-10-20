// p64 -- the intro animations (p101, spec 15.1): the boot animation is one of a suite of
// short procedural reveals of the p64 logo, drawn from two embedded layers (the panel with
// the bird, the wordmark on top) that each animation moves, fades or uncovers in its own
// way. Every animation is a pure function of the window's normalised time and a per-boot
// seed: the registry, the names and the drawing are host-tested; the shell that paces the
// frames lives in main/show.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "p64/gfx/frame.hpp"

namespace p64::intro {

// The frame cadence (50 fps, decided 2026-10-20: a 64x64 render costs well under the
// 20 ms, and 60 fps has shown rare late flips in Quality mode).
constexpr uint32_t kFrameMs = 20;
// Every animation has reached the finished logo by this fraction of the window and holds
// it for the rest (no separate hold setting; decided 2026-10-20).
constexpr float kHoldFrom = 0.85f;

// Working memory for the animations that build a map per frame (a Voronoi cell map):
// the caller owns it, so nothing is held between runs.
constexpr size_t kScratchBytes = 4096;

struct Context {
  uint32_t seed = 0;             // per boot; the only source of randomness
  gfx::Rgb background{0, 0, 0};  // under the layers (display.background)
  uint8_t *scratch = nullptr;    // kScratchBytes, or null: those animations draw plainly
};

int count();
const char *name(int index);  // snake_case; null out of range
int find(const char *name);   // -1 when unknown

// The frame at time `t` in [0, 1] of the window: the background alone at 0, the finished
// logo from kHoldFrom on. Deterministic for (index, ctx, t).
void render(int index, gfx::Frame &frame, const Context &ctx, float t);
// The finished logo: background, layer 0, layer 1 at their home positions.
void finished(gfx::Frame &frame, const Context &ctx);

}  // namespace p64::intro

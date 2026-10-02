// p64 patch: the bit-plane arithmetic of the p64 refresh profiles as pure code, so the
// host tests can check it (tests/host/unit/bcm.cpp; review of 2026-09-22). GdmaDma calls
// these; nothing here touches the hardware.
//
// A plane above the transition bit is sent 2^(bit - transition - 1) times per frame with
// the full output-enable window; a plane at or below it is sent once with half the window
// of the plane above, which restores its binary weight. A plane's weight is its window
// (pixel clocks) times its repetitions. The LUT maps each 8-bit input to the code whose
// weight is nearest the gamma table's target, which only works when the weights are
// superincreasing (each plane weighs at least the planes below it together): ten planes
// at transition bit 6 broke that on 2026-09-20 and every input above 13 mapped to code 3.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hub75::p64bcm {

inline uint32_t plane_reps(int bit, int transition_bit) {
  return bit <= transition_bit ? 1u : (1u << (bit - transition_bit - 1));
}

// The output-enable window of each of `planes` planes, in pixel clocks, for a row of
// `max_pixels` usable clocks at `effective_brightness` (0..255 after the driver's curve).
// Keeps the weights superincreasing: a plane that would weigh less than the planes below
// it is blanked (it still costs its transmission).
inline void plane_windows(int max_pixels, int effective_brightness, int planes, int transition_bit, int out[]) {
  uint32_t weight_below = 0;
  for (int bit = 0; bit < planes; bit++) {
    int display_pixels = (max_pixels * effective_brightness) >> 8;
    if (bit <= transition_bit) display_pixels >>= (transition_bit + 1 - bit);
    // The upstream fallback: at very low brightness the top planes keep one clock.
    const int min_bit_for_display = std::max(0, planes - 1 - (effective_brightness >> 4));
    if (effective_brightness > 0 && display_pixels == 0 && bit >= min_bit_for_display) display_pixels = 1;
    // At least one clock of blanking against ghosting near the latch pulse.
    display_pixels = std::min(display_pixels, max_pixels - 1);
    const uint32_t reps = plane_reps(bit, transition_bit);
    if (static_cast<uint32_t>(display_pixels) * reps < weight_below) display_pixels = 0;
    weight_below += static_cast<uint32_t>(display_pixels) * reps;
    out[bit] = display_pixels;
  }
}

// Which window goes into which buffer of a row's chain (2026-10-02). The panel's shift
// registers latch a buffer's data at its last clock, so while buffer k is clocked out the
// LEDs show the data of the buffer sent before it, gated by buffer k's output-enable bits
// (the reason buffer 0 carries the previous row's address). The window of plane b
// therefore belongs in the buffer after it, and buffer 0 gets the top plane's, which is
// still on the previous row. Until this was found the windows sat in their own plane's
// buffer and the data weighed 3 7 15 31 62 62 124 248 496 931 clocks in Quality instead
// of 1 3 7 15 31 62 124 248 496 992: darks two to three times too bright and the light
// dropping by half from input 51 to 52 (seen on the panel with four grey bands).
inline void buffer_windows(const int plane_window[], int planes, int out[]) {
  for (int k = 0; k < planes; k++) out[k] = plane_window[(k + planes - 1) % planes];
}

// The light of each plane's data, in pixel clocks per frame, given the windows in the
// buffers: a plane's data is on the LEDs during every send of its own buffer but the
// first, and during the first send of the next buffer. With buffer_windows() this is the
// plane's window times its repetitions as long as the planes sent more than once share
// one window, which plane_windows() gives at every level the driver uses (17 and up).
inline void data_weights(const int buffer_window[], int planes, int transition_bit, uint32_t out[]) {
  for (int bit = 0; bit < planes; bit++) {
    out[bit] = (plane_reps(bit, transition_bit) - 1) * static_cast<uint32_t>(buffer_window[bit]) +
               static_cast<uint32_t>(buffer_window[(bit + 1) % planes]);
  }
}

// The light of a profile at `effective_brightness`: the sum over the planes of window
// times repetitions, in pixel clocks per frame (1979 for Quality at 255).
inline uint32_t weight_total(int max_pixels, int effective_brightness, int planes, int transition_bit) {
  int windows[16] = {};
  plane_windows(max_pixels, effective_brightness, planes, transition_bit, windows);
  uint32_t total = 0;
  for (int bit = 0; bit < planes; bit++) total += static_cast<uint32_t>(windows[bit]) * plane_reps(bit, transition_bit);
  return total;
}

// How a profile emits a requested share of its full light (spec 3.2, 2026-09-27): the
// output-enable level, and the LUT scale that closes the gap between that level's light
// and the request. The output-enable windows are whole pixel clocks, so their levels are
// coarse (59 on this panel, 25 % apart at the bottom) and stop at the driver's floor
// (`min_effective`, 17 here: four clocks on the top plane). The plan takes the smallest
// level whose light reaches the request and scales the LUT's targets down by the rest,
// so every request lands exactly and the scale stays between about 0.75 and 1 above the
// floor; below the floor the level stays at the floor and the scale alone dims, at the
// cost of one bit of tonal depth per halving (the codes that remain are the low planes).
struct LightPlan {
  int effective = 0;        // the output-enable level, 0..255 (0 = blank)
  uint32_t weight = 0;      // the light of that level, clocks per frame
  uint32_t scale_q16 = 0;   // the LUT scale, 16.16 (65536 = none)
};

inline LightPlan plan_light(int max_pixels, int planes, int transition_bit, int min_effective, uint32_t light_q16) {
  LightPlan plan;
  if (light_q16 == 0) return plan;
  if (light_q16 > 65536u) light_q16 = 65536u;
  const uint32_t full = weight_total(max_pixels, 255, planes, transition_bit);
  if (light_q16 == 65536u) {  // the top levels tie (their windows saturate): report 255
    plan.effective = 255;
    plan.weight = full;
    plan.scale_q16 = 65536u;
    return plan;
  }
  const double target = static_cast<double>(full) * light_q16 / 65536.0;
  int level = min_effective < 1 ? 1 : min_effective;
  uint32_t weight = weight_total(max_pixels, level, planes, transition_bit);
  while (level < 255 && static_cast<double>(weight) < target) {
    level++;
    weight = weight_total(max_pixels, level, planes, transition_bit);
  }
  plan.effective = level;
  plan.weight = weight;
  double scale = weight ? target / static_cast<double>(weight) : 1.0;
  if (scale > 1.0) scale = 1.0;
  plan.scale_q16 = static_cast<uint32_t>(scale * 65536.0 + 0.5);
  if (plan.scale_q16 == 0) plan.scale_q16 = 1;
  return plan;
}

// Fits `lut_out[256]` to the plane weights: input i gets the code whose weight is nearest
// ideal[i] scaled from 0..ideal_max to 0..total weight times `scale_q16` (16.16, 65536 =
// the full light; a smaller scale is the software dimming of plan_light). Returns the
// number of distinct codes (0 when every plane is dark).
//
// The walk goes through the codes built from the lit planes only, in order. With
// superincreasing weights that order is the order of their light, so a walk that stops at
// the first code farther from the target than the last one finds the nearest. A blanked
// plane (weight 0) must be left out of that order: code 4 over the planes 1, 1, 0 weighs
// less than code 3, and the walk stopped there for every input above 13 until the review
// of 2026-09-22 found it (the blanking of 2026-09-20 had kept the weights in order but not
// the walk). Profiles without a blanked plane walk exactly the codes they walked before.
inline unsigned fit_lut(const uint16_t *ideal, uint32_t ideal_max, const uint32_t *weights, int planes,
                        uint16_t *lut_out, uint32_t scale_q16 = 65536u) {
  uint32_t total = 0;
  int lit[16];
  int lit_count = 0;
  for (int bit = 0; bit < planes; bit++) {
    total += weights[bit];
    if (weights[bit]) lit[lit_count++] = bit;
  }
  if (total == 0) return 0;
  const uint32_t max_step = (1u << lit_count) - 1;
  auto code_of = [&](uint32_t step) {  // spread the step's bits onto the lit planes
    uint32_t code = 0;
    for (int k = 0; k < lit_count; k++) {
      if (step & (1u << k)) code |= 1u << lit[k];
    }
    return code;
  };
  auto weight_of = [&](uint32_t step) {
    uint32_t w = 0;
    for (int k = 0; k < lit_count; k++) {
      if (step & (1u << k)) w += weights[lit[k]];
    }
    return w;
  };
  uint32_t step = 0;
  double w_step = 0;
  for (int i = 0; i < 256; i++) {
    const double target = static_cast<double>(ideal[i]) * total / ideal_max * scale_q16 / 65536.0;
    while (step < max_step) {
      const double w_next = weight_of(step + 1);
      if (std::fabs(w_next - target) > std::fabs(w_step - target)) break;
      step++;
      w_step = w_next;
    }
    lut_out[i] = static_cast<uint16_t>(code_of(step));
  }
  unsigned distinct = 1;
  for (int i = 1; i < 256; i++) {
    if (lut_out[i] != lut_out[i - 1]) distinct++;
  }
  return distinct;
}

}  // namespace hub75::p64bcm

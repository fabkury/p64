#include "p64/inputs/knob_rules.hpp"

namespace p64::inputs {

uint8_t brightness_after(uint8_t current, int32_t detents) {
  int value = current < 1 ? 1 : current;
  for (; detents > 0; --detents) {
    int step = (value + 5) / 10;  // 10 %, rounded
    if (step < 1) step = 1;
    value += step;
    if (value > 255) {
      value = 255;
      break;
    }
  }
  for (; detents < 0; ++detents) {
    int step = (value + 5) / 10;
    if (step < 1) step = 1;
    value -= step;
    if (value < 1) {
      value = 1;
      break;
    }
  }
  return static_cast<uint8_t>(value);
}

}  // namespace p64::inputs

#include "status_screens.hpp"

namespace p64::status_screens {
namespace {

using gfx::Frame;
using gfx::Rgb;

// A 5x7 question mark, drawn at 3x in the middle of the panel.
constexpr uint8_t kQuestion[7] = {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100};

void glyph(Frame &f, int x0, int y0, int scale, Rgb colour) {
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 5; ++col) {
      if (kQuestion[row] & (1 << (4 - col))) f.fill_rect(x0 + col * scale, y0 + row * scale, scale, scale, colour);
    }
  }
}

}  // namespace

void no_artwork(Frame &frame, const std::string &reason) {
  frame.clear(gfx::kBlack);
  const Rgb border{28, 28, 34};
  frame.fill_rect(2, 2, 60, 1, border);
  frame.fill_rect(2, 61, 60, 1, border);
  frame.fill_rect(2, 2, 1, 60, border);
  frame.fill_rect(61, 2, 1, 60, border);
  glyph(frame, 32 - 7, 32 - 12, 3, Rgb{90, 90, 100});
  Rgb bar{70, 70, 70};  // unknown reason
  if (reason == "no card") {
    bar = Rgb{160, 40, 40};
  } else if (reason == "offline" || reason.rfind("Makapix", 0) == 0) {
    bar = Rgb{40, 80, 160};
  } else if (reason == "needs pairing") {
    bar = Rgb{140, 60, 160};
  } else if (reason == "empty") {
    bar = Rgb{150, 120, 30};
  }
  frame.fill_rect(20, 50, 24, 3, bar);
}

void black(Frame &frame) { frame.clear(gfx::kBlack); }

}  // namespace p64::status_screens

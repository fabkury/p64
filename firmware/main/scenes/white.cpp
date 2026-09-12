#include "scenes/white.hpp"

namespace p64 {

void WhiteScene::enter(Display &display, Frame &frame) {
  frame.clear(kWhite);
  display.set_brightness(max_brightness());
  display.present(frame);
}

bool WhiteScene::render(Display &, Frame &, const FrameInfo &) {
  return false;  // nothing changes after the first frame
}

}  // namespace p64

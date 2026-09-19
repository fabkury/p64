// p64 -- status screens (spec 6.4): what the panel shows when the user must act or
// wait. Until the font pipeline lands (M7) they are symbols rather than text: the "no
// artwork" screen is a dim frame with a question mark and a colour-coded reason bar.
#pragma once

#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::status_screens {

// reason: "no card", "offline", "needs pairing", "empty", or anything else (grey).
void no_artwork(gfx::Frame &frame, const std::string &reason);
void black(gfx::Frame &frame);

}  // namespace p64::status_screens

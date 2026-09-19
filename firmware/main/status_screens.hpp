// p64 -- status screens (spec 6.4): what the panel shows when the user must act or
// wait, drawn with the built-in 5x7 font (the bundled artist fonts arrive with M7).
#pragma once

#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::status_screens {

// "NO ARTWORK" and the reason ("no card", "offline", "needs pairing", "empty", ...).
void no_artwork(gfx::Frame &frame, const std::string &reason);
void black(gfx::Frame &frame);
// The Makapix pairing code, large, in two rows of three characters.
void pairing_code(gfx::Frame &frame, const std::string &code);
void paired(gfx::Frame &frame);
// Hostname and IP address after joining a network.
void connected(gfx::Frame &frame, const std::string &hostname, const std::string &ip);

}  // namespace p64::status_screens

// p64 -- status screens (spec 6.4): what the panel shows when the user must act or
// wait, in the firmware's own fonts: Everyday Standard for regular text, Everyday Ample
// for the short lines that deserve a bigger letter (gfx::fonts::system_font() and
// system_large_font()). Sentence case. Host-tested (tests/host/unit/app.cpp).
#pragma once

#include <cstdint>
#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::status_screens {

// "No artwork" and the reason ("no card", "offline", "needs pairing", "empty", ...).
void no_artwork(gfx::Frame &frame, const std::string &reason);
// The BOOT-hold factory reset countdown: 3, 2, 1 (0 = "Erasing").
void countdown(gfx::Frame &frame, int seconds);
void black(gfx::Frame &frame);
// The Makapix pairing code, large, in two rows of three characters.
void pairing_code(gfx::Frame &frame, const std::string &code);
void paired(gfx::Frame &frame);
// Hostname and IP address after joining a network.
void connected(gfx::Frame &frame, const std::string &hostname, const std::string &ip);
// The Stream state with no frames arriving: hostname, IP and the ports (spec 6.3).
void stream_waiting(gfx::Frame &frame, const std::string &hostname, const std::string &ip, int ddp_port, int raw_port);

// Setup mode (spec 6.4, 10.1): three pages the show cycles every kSetupPageUs: "Wi-Fi
// setup", the access point to join, the address to open.
constexpr int kSetupPages = 3;
constexpr long long kSetupPageUs = 3000000;
void setup_page(gfx::Frame &frame, int page, const std::string &ap_ssid, const std::string &ap_ip);

// A firmware update (spec 6.4, 15.2): the version and a progress bar while it downloads
// and verifies, "ready" once written (until the reboot), "failed" with the reason.
struct UpdateView {
  enum class Phase : uint8_t { Downloading, Verifying, Ready, Failed };
  Phase phase = Phase::Downloading;
  std::string version;  // the release being installed ("" when installed from a URL)
  int percent = -1;     // download progress, -1 while the size is unknown
  std::string error;
};
void update(gfx::Frame &frame, const UpdateView &view);

}  // namespace p64::status_screens

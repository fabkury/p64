// p64 -- streams (spec section 8, ADR 0007): pixels arriving over DDP (UDP 4048) and the
// raw p64 format (UDP 4064), assembled by byte offset, converted and scaled into a panel
// frame; the latest complete frame wins. A FrameSource hands frames to the player as
// they land, with the freshest frame at each 60 fps slot. Stream start and end (the
// silence timeout) go out on the event bus (StreamStarted, StreamEnded) so the show can
// take the panel over and give it back.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "cJSON.h"
#include "p64/playback/frame_source.hpp"

namespace p64::stream {

// Starts the listener task (ports and enables from the settings; reopened on change).
bool start();
// The source that presents each complete frame as it arrives (one instance, PSRAM).
std::shared_ptr<playback::FrameSource> source();
// True while frames arrive (the silence timeout has not elapsed since the last one).
bool active();
// Makes a blocked next_frame() return at once (the show calls it before replacing the
// stream on the player, so the swap away is immediate).
void wake();

struct Status {
  bool active = false;
  std::string protocol;  // "ddp", "raw" or ""
  int width = 0, height = 0;
  uint32_t frames = 0;           // complete frames received
  uint32_t incomplete = 0;       // frames dropped: a newer one began, or holes at the last chunk
  uint32_t rejected = 0;         // datagrams with a bad header, size or offset
  uint32_t datagrams = 0;
  uint32_t lost = 0;             // raw: sequence gaps
  float fps = 0;                 // complete frames per second, over the last second
  uint32_t last_latency_us = 0;  // last chunk arrival -> the player took the frame
  std::string sender;            // the last sender's address
  bool ddp_listening = false, raw_listening = false;
};
Status status();
cJSON *status_json();

}  // namespace p64::stream

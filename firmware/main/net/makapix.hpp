// p64 -- Makapix Club client: fetches one random promoted GIF that fits the panel.
//
// Runs as a background task on the Wi-Fi core. The scene asks for the next artwork
// with request_next() and later collects it with take_ready(); the task talks to
// https://makapix.club anonymously:
//   GET /api/post?promoted=true&sort=random&limit=1&width_max=W&height_max=H&file_format=gif
//   GET /api/d/{public_sqid}.gif
// TLS needs a valid clock, so the first fetch waits for the NTP sync.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace p64::makapix {

struct Artwork {
  std::string sqid;
  std::string title;
  std::string artist;
  int width = 0;
  int height = 0;
  std::vector<uint8_t> gif;  // the GIF file
};

// Starts the fetcher task. No-op when Makapix is disabled in menuconfig.
void start();

// Asks the task to fetch one artwork in the background. Ignored while a fetch is in
// flight or an artwork is already waiting to be taken.
void request_next();

// Moves the waiting artwork out, if any.
bool take_ready(Artwork &out);

}  // namespace p64::makapix

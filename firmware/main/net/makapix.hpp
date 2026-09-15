// p64 -- Makapix Club client and GIF downloader.
//
// A background task on the Wi-Fi core with two jobs:
//  - the show's rotation: request_next() fetches one random promoted GIF that fits the
//    panel, collected later with take_ready(); the scene calls it again (every few
//    seconds while it waits) after a fetch came back empty, so failures retry forever:
//      GET /api/post?promoted=true&sort=random&limit=1&width_max=W&height_max=H&file_format=gif
//      GET /api/d/{public_sqid}.gif
//  - on-demand playback for the web control (net/web): request_play() downloads a given
//    post's GIF or any GIF URL, or reads a file from the microSD card, collected with
//    take_play(); a new request replaces one still waiting, and web requests go ahead of
//    the rotation.
// TLS needs a valid clock, so HTTPS fetches wait for the NTP sync.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace p64::makapix {

struct Artwork {
  std::string sqid;    // Makapix post; empty for a plain URL
  std::string url;     // the GIF's URL for URL requests; empty for Makapix posts
  std::string file;    // file name on the microSD card for card requests
  std::string title;   // known for rotation picks only
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

// While paused the fetcher holds new requests (in-flight downloads finish). Used by
// the speed test to keep the link to itself.
void set_paused(bool paused);
bool busy();  // a fetch is in progress

// --- On-demand playback ---------------------------------------------------------

struct PlayRequest {
  std::string sqid;      // a Makapix post, or
  std::string url;       // a GIF URL (http or https), or
  std::string file;      // a GIF file in the microSD card's root
  uint32_t seconds = 0;  // playback time; 0 = until the next request
};

enum class PlayState { idle, queued, downloading, ready, failed };

struct PlayStatus {
  uint32_t id = 0;  // of the latest request; 0 = none yet
  PlayState state = PlayState::idle;
  std::string target;  // sqid or URL of that request
  std::string error;   // why it failed
};

// Queues the download and returns the request id (0 when the fetcher is not running).
// A request still waiting, or a result nobody took yet, is replaced.
uint32_t request_play(const PlayRequest &req);

// Moves a finished on-demand download out; `seconds` and `id` are the request's.
bool take_play(Artwork &out, uint32_t &seconds, uint32_t &id);

// Drops a waiting request or result (used by /stop).
void cancel_play();

// The scene reports a downloaded file that does not decode.
void set_play_error(uint32_t id, const char *error);

PlayStatus play_status();

}  // namespace p64::makapix

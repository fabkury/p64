// p64 -- web control: a small HTTP server reachable as http://p64.local/ (mDNS).
//
//   GET|POST /play?post=<Makapix post URL or sqid>&seconds=N
//   GET|POST /play?url=<GIF URL>&seconds=N      -> 202, the GIF is queued for playback
//   GET /stop                                    -> ends on-demand playback, the show resumes
//   GET /status                                  -> JSON: what is playing, last request, link
//   GET /                                        -> a form for phones
//
// The server answers at once; the Makapix fetcher task downloads the file and the GIF
// scene picks it up. seconds=0 keeps the GIF up until the next request; the default
// is CONFIG_P64_WEB_DEFAULT_SECONDS.
#pragma once

#include <cstdint>
#include <string>

namespace p64::web {

// What the scene is showing, published by the scene for /status.
struct NowPlaying {
  std::string name;        // log name: asset file, "makapix <sqid>" or the URL
  std::string source;      // "embedded", "makapix" or "url"
  std::string url;         // Makapix page or the GIF URL, when known
  int width = 0;
  int height = 0;
  size_t bytes = 0;
  bool on_demand = false;  // requested through /play
  int64_t started_us = 0;  // esp_timer time
  int64_t until_us = 0;    // esp_timer time when on-demand playback ends; 0 = until the next request
};

// Starts mDNS and the HTTP server. No-op when disabled in menuconfig.
void start();

// Scene -> status page.
void publish(const NowPlaying &now);

// True once after each /stop (the scene polls it).
bool take_stop();

}  // namespace p64::web

// p64 -- the GIF show: starts on a random GIF from the microSD card (black with the
// clock when the card has none), switches to the first Makapix Club artwork the moment
// it has downloaded, then to a fresh one every 30 s, or as soon as the next download
// lands when it is late (the current artwork stays up meanwhile; nothing but fresh
// downloads play after the startup one). Plays at the GIFs' intended speed, with the
// wall clock top-left. A GIF requested through the web control (net/web) pre-empts the
// rotation for its requested time, or until /stop or the next request. Menuconfig can
// turn the show into the frame-rate test (ignore delays, show fps top-right).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gif_player.hpp"
#include "net/makapix.hpp"
#include "net/web.hpp"
#include "scene.hpp"

namespace p64 {

class GifShowScene : public Scene {
 public:
  const char *name() const override;
  uint32_t duration_ms() const override { return kRunForever; }
  void enter(Display &display, Frame &frame) override;
  bool render(Display &display, Frame &frame, const FrameInfo &info) override;

 private:
  bool play_card_random(uint32_t now_ms);
  bool play_download(makapix::Artwork &&art, uint32_t now_ms);
  bool open_current(const uint8_t *data, size_t size, uint32_t now_ms);
  bool rotate(uint32_t now_ms);
  void next_slot(uint32_t now_ms);
  void drop_current(uint32_t now_ms);
  void start_on_demand(makapix::Artwork &&art, uint32_t seconds, uint32_t id, uint32_t now_ms);
  void start_pattern(uint32_t now_ms);
  void start_playlist(std::vector<std::string> &&files, uint32_t seconds, uint32_t now_ms);
  void playlist_request(size_t index);
  void playlist_request_next();
  void end_playlist(const char *why);
  void draw_pattern(Frame &frame) const;
  void end_on_demand(uint32_t now_ms, const char *why);
  void publish_now_playing() const;
  void log_gif_stats(uint32_t now_ms) const;
  bool decode_next(uint32_t now_ms);
  void draw_overlays(Frame &frame, float fps) const;

  std::string current_name_;     // for the log: card file name, Makapix sqid or URL
  makapix::Artwork download_;    // owns the bytes of the artwork being played, if any
  makapix::Artwork rotation_;    // the rotation's artwork set aside while a web request plays
  size_t current_bytes_ = 0;     // size of the GIF being played (for the status page)
  bool swap_asap_ = true;        // switch to the next download the moment it lands (no 30 s wait)
  bool waiting_logged_ = false;  // "slot over, download not ready" logged for this slot
  uint32_t next_request_ms_ = 0;  // when to nudge the fetcher again while waiting
  bool on_demand_ = false;       // playing a web request rather than the rotation
  bool pattern_ = false;         // showing the tone test pattern (a web request too)
  // Card playlist (/sd/play): the files play in turn as on-demand requests until /stop.
  std::vector<std::string> playlist_;
  size_t playlist_index_ = 0;
  uint32_t playlist_seconds_ = 0;
  bool playlist_active_ = false;
  uint32_t playlist_request_id_ = 0;  // pending request for the next file, 0 = none
  size_t playlist_failures_ = 0;      // consecutive files that could not be played
  uint32_t on_demand_id_ = 0;
  bool on_demand_indefinite_ = false;
  uint32_t on_demand_until_ms_ = 0;  // scene time; unused when indefinite
  int64_t on_demand_until_us_ = 0;   // esp_timer time for the status page; 0 = indefinite
  uint32_t gif_start_ms_ = 0;
  uint32_t gif_frames_ = 0;
  uint64_t gif_decode_us_ = 0;
  uint32_t next_frame_ms_ = 0;   // when the next GIF frame is due
  bool single_frame_ = false;    // still image: decode once, then hold
  char clock_text_[8] = "--:--";
  GifPlayer player_;
  Scaler scaler_;
};

}  // namespace p64

// p64 -- the GIF show: starts on a random embedded GIF, then every 30 s switches to
// the artwork the Makapix fetcher downloaded in the meantime (a random promoted GIF
// that fits the panel), or to another embedded GIF when nothing arrived. Plays at the
// GIFs' intended speed, with the wall clock top-left. A GIF requested through the web
// control (net/web) pre-empts the rotation for its requested time, or until /stop or
// the next request. Menuconfig can turn the show into the frame-rate test (ignore
// delays, show fps top-right).
#pragma once

#include <cstdint>
#include <string>

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
  bool play_embedded(uint32_t now_ms);
  bool play_download(makapix::Artwork &&art, uint32_t now_ms);
  bool open_current(const uint8_t *data, size_t size, uint32_t now_ms);
  void next_slot(uint32_t now_ms);
  void start_on_demand(makapix::Artwork &&art, uint32_t seconds, uint32_t id, uint32_t now_ms);
  void end_on_demand(uint32_t now_ms, const char *why);
  void publish_now_playing() const;
  void log_gif_stats(uint32_t now_ms) const;
  bool decode_next(uint32_t now_ms);
  void draw_overlays(Frame &frame, float fps) const;

  std::string current_name_;     // for the log: asset file name or Makapix sqid
  size_t embedded_index_ = 0;    // last embedded GIF shown (avoid repeating it)
  makapix::Artwork download_;    // owns the bytes of the artwork being played, if any
  size_t current_bytes_ = 0;     // size of the GIF being played (for the status page)
  bool on_demand_ = false;       // playing a web request rather than the rotation
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

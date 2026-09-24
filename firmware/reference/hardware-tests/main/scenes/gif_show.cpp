#include "scenes/gif_show.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "font3x5.hpp"
#include "net/clock.hpp"
#include "net/web.hpp"
#include "sdcard.hpp"

namespace p64 {
namespace {

constexpr const char *TAG = "gif";
constexpr uint32_t kDwellMs = static_cast<uint32_t>(CONFIG_P64_GIF_DWELL_S) * 1000;
// While a slot is over and no download is ready, the fetcher is nudged this often (a
// nudge is a no-op while it is still trying; after it gave up, this is the retry rate).
constexpr uint32_t kRetryMs = 5000;
// Same caps as the fetcher applies to card requests.
constexpr size_t kMaxCardBytes = CONFIG_P64_WEB_MAX_BYTES;
constexpr int kMaxCardDim = CONFIG_P64_WEB_MAX_DIMENSION;
constexpr Rgb kText{255, 255, 255};

#if defined(CONFIG_P64_GIF_MAX_SPEED)
constexpr bool kMaxSpeed = true;
#else
constexpr bool kMaxSpeed = false;
#endif
#if defined(CONFIG_P64_SHOW_FPS)
constexpr bool kShowFps = true;
#else
constexpr bool kShowFps = false;
#endif

// Browser rule for frame delays: anything under 20 ms is shown for 100 ms, which is
// what the artist saw in a browser and on Makapix's own viewer.
uint32_t effective_delay_ms(int delay_ms) { return delay_ms < 20 ? 100 : static_cast<uint32_t>(delay_ms); }

// Opacity of the box behind overlay text: 70 % black, so the artwork stays faintly
// visible through it while the digits keep their contrast (50 % was too see-through).
constexpr uint8_t kBoxAlpha = 179;

// Draws text with its top-left at (x, y) on a translucent black box with 1 pixel of
// padding.
void draw_boxed_text(Frame &frame, int x, int y, const char *text) {
  const int w = text_width_3x5(text);
  frame.blend_rect(x - 1, y - 1, w + 2, 7, kBlack, kBoxAlpha);
  draw_text_3x5(frame, x, y, text, kText);
}

}  // namespace

const char *GifShowScene::name() const { return kMaxSpeed ? "gif playback, max speed" : "gif show"; }

void GifShowScene::enter(Display &display, Frame &frame) {
  ESP_LOGI(TAG, "%lu s per artwork, frame delays %s", static_cast<unsigned long>(kDwellMs / 1000),
           kMaxSpeed ? "ignored" : "honoured (min 100 ms when under 20)");
  display.set_brightness(max_brightness());
  // The startup artwork is read from the card right here: nothing renders yet, so the
  // read (a few ms) cannot stall the panel, and the fetcher may not even be running
  // (it needs Wi-Fi). From now on only fresh Makapix downloads play.
  if (!play_card_random(0)) ESP_LOGI(TAG, "nothing to show until the first Makapix artwork arrives");
  swap_asap_ = true;
  makapix::request_next();
  frame.clear();
  if (player_.is_open()) {
    decode_next(0);
    scaler_.scale(player_.canvas(), frame.pixels());
  }
  draw_overlays(frame, 0.0f);
  display.present(frame);
}

bool GifShowScene::open_current(const uint8_t *data, size_t size, uint32_t now_ms) {
  gif_start_ms_ = now_ms;
  gif_frames_ = 0;
  gif_decode_us_ = 0;
  next_frame_ms_ = now_ms;
  single_frame_ = false;
  current_bytes_ = size;
  if (!player_.open(data, size)) {
    ESP_LOGW(TAG, "%s: cannot open (AnimatedGIF error %d), skipped", current_name_.c_str(), player_.last_error());
    return false;
  }
  scaler_.configure(player_.width(), player_.height(), kWidth, kHeight);
  return true;
}

// Plays a random GIF from the card's root, trying the others in turn if the pick does
// not read or open; false when the card has no playable GIF.
bool GifShowScene::play_card_random(uint32_t now_ms) {
  const std::vector<sdcard::FileInfo> files = sdcard::list_gifs();
  if (files.empty()) {
    ESP_LOGI(TAG, "no GIF on the card (%s)", sdcard::mounted() ? "empty" : "not mounted");
    return false;
  }
  const size_t first = esp_random() % files.size();
  for (size_t i = 0; i < files.size(); ++i) {
    const std::string &name = files[(first + i) % files.size()].name;
    makapix::Artwork art;
    art.file = name;
    std::string error;
    if (!sdcard::read_file(name, art.gif, kMaxCardBytes, error)) {
      ESP_LOGW(TAG, "card %s: %s, skipped", name.c_str(), error.c_str());
      continue;
    }
    if (art.gif.size() < 10 || std::memcmp(art.gif.data(), "GIF8", 4) != 0) {
      ESP_LOGW(TAG, "card %s: not a GIF file, skipped", name.c_str());
      continue;
    }
    art.width = art.gif[6] | (art.gif[7] << 8);
    art.height = art.gif[8] | (art.gif[9] << 8);
    if (art.width > kMaxCardDim || art.height > kMaxCardDim) {
      ESP_LOGW(TAG, "card %s: %dx%d is larger than %d px, skipped", name.c_str(), art.width, art.height, kMaxCardDim);
      continue;
    }
    if (play_download(std::move(art), now_ms)) return true;
  }
  ESP_LOGW(TAG, "none of the %u GIFs on the card plays", static_cast<unsigned>(files.size()));
  return false;
}

// Plays `art` (a card file, a Makapix post or a URL download), taking ownership of
// its bytes.
bool GifShowScene::play_download(makapix::Artwork &&art, uint32_t now_ms) {
  player_.close();  // it may still point into the previous download's bytes
  pattern_ = false;
  download_ = std::move(art);
  current_name_ = !download_.file.empty() ? "card " + download_.file
                  : download_.sqid.empty() ? download_.url
                                           : "makapix " + download_.sqid;
  if (!open_current(download_.gif.data(), download_.gif.size(), now_ms)) return false;
  if (!download_.file.empty()) {
    ESP_LOGI(TAG, "playing card file %s (%dx%d, %u bytes) at %dx%d", download_.file.c_str(), player_.width(),
             player_.height(), static_cast<unsigned>(download_.gif.size()), scaler_.out_w(), scaler_.out_h());
  } else if (download_.sqid.empty()) {
    ESP_LOGI(TAG, "playing %s (%dx%d, %u bytes) at %dx%d", download_.url.c_str(), player_.width(), player_.height(),
             static_cast<unsigned>(download_.gif.size()), scaler_.out_w(), scaler_.out_h());
  } else if (download_.title.empty()) {  // on-demand posts come without metadata
    ESP_LOGI(TAG, "playing makapix %s (%dx%d, %u bytes) at %dx%d, https://%s/p/%s", download_.sqid.c_str(),
             player_.width(), player_.height(), static_cast<unsigned>(download_.gif.size()), scaler_.out_w(),
             scaler_.out_h(), CONFIG_P64_MAKAPIX_HOST, download_.sqid.c_str());
  } else {
    ESP_LOGI(TAG, "playing makapix %s \"%s\" by %s (%dx%d, %u bytes) at %dx%d, https://%s/p/%s",
             download_.sqid.c_str(), download_.title.c_str(), download_.artist.c_str(), player_.width(),
             player_.height(), static_cast<unsigned>(download_.gif.size()), scaler_.out_w(), scaler_.out_h(),
             CONFIG_P64_MAKAPIX_HOST, download_.sqid.c_str());
  }
  publish_now_playing();
  return true;
}

// The rotation, called every frame while no web request plays: once the slot is over
// (30 s, or at once while swap_asap_), switch to the download the moment one is ready;
// meanwhile keep the current artwork up and keep the fetcher trying. True when what
// plays changed.
bool GifShowScene::rotate(uint32_t now_ms) {
  const bool slot_over = swap_asap_ || now_ms - gif_start_ms_ >= kDwellMs;
  if (!slot_over) return false;
  makapix::Artwork art;
  if (makapix::take_ready(art)) {
    log_gif_stats(now_ms);
    if (play_download(std::move(art), now_ms)) {
      swap_asap_ = false;
    } else {
      drop_current(now_ms);
    }
    waiting_logged_ = false;
    makapix::request_next();  // start on the one after, so it is ready when this slot ends
    return true;
  }
  if (!waiting_logged_ && !swap_asap_) {
    ESP_LOGI(TAG, "slot over, next artwork not downloaded yet; %s stays up until it lands", current_name_.c_str());
    waiting_logged_ = true;
  }
  if (static_cast<int32_t>(now_ms - next_request_ms_) >= 0) {
    makapix::request_next();
    next_request_ms_ = now_ms + kRetryMs;
  }
  return false;
}

// A web request ended: the fresh download if one is waiting, else the rotation's
// artwork that was set aside (to be replaced as soon as a download lands), else black.
void GifShowScene::next_slot(uint32_t now_ms) {
  log_gif_stats(now_ms);
  on_demand_ = false;
  on_demand_indefinite_ = false;
  pattern_ = false;
  makapix::Artwork art;
  if (makapix::take_ready(art) && play_download(std::move(art), now_ms)) {
    swap_asap_ = false;
  } else if (!rotation_.gif.empty()) {
    ESP_LOGI(TAG, "back to the rotation's artwork until the next download lands");
    swap_asap_ = true;
    if (!play_download(std::move(rotation_), now_ms)) drop_current(now_ms);
  } else {
    drop_current(now_ms);
  }
  rotation_ = makapix::Artwork{};
  waiting_logged_ = false;
  makapix::request_next();
}

// Nothing plays: black until the next download.
void GifShowScene::drop_current(uint32_t now_ms) {
  player_.close();
  download_ = makapix::Artwork{};
  current_name_.clear();
  current_bytes_ = 0;
  gif_start_ms_ = now_ms;
  gif_frames_ = 0;
  single_frame_ = true;
  swap_asap_ = true;
  publish_now_playing();
}

void GifShowScene::log_gif_stats(uint32_t now_ms) const {
  if (!player_.is_open() || gif_frames_ == 0) return;
  const float seconds = static_cast<float>(now_ms - gif_start_ms_) / 1000.0f;
  ESP_LOGI(TAG, "%s: %" PRIu32 " frames in %.1f s = %.1f fps, %" PRIu32 " loops, decode+scale %.3f ms/frame",
           current_name_.c_str(), gif_frames_, seconds, static_cast<float>(gif_frames_) / seconds, player_.loops(),
           static_cast<float>(gif_decode_us_) / static_cast<float>(gif_frames_) / 1000.0f);
}

// Decodes the next frame into the player's canvas and schedules the one after.
// Returns false when the GIF is broken (the caller moves on).
bool GifShowScene::decode_next(uint32_t now_ms) {
  if (!player_.is_open()) return false;
  const int64_t t0 = esp_timer_get_time();
  if (!player_.next_frame()) return false;
  gif_decode_us_ += static_cast<uint64_t>(esp_timer_get_time() - t0);
  ++gif_frames_;
  single_frame_ = (gif_frames_ == 1 && player_.at_end());
  next_frame_ms_ = now_ms + effective_delay_ms(player_.last_delay_ms());
  return true;
}

bool GifShowScene::render(Display &, Frame &frame, const FrameInfo &info) {
  bool dirty = false;

  // Web control: a downloaded request pre-empts whatever plays; /stop or its time
  // running out hands the panel back to the rotation (or to the next card file when a
  // playlist is running).
  {
    std::vector<std::string> files;
    uint32_t seconds = 0;
    if (web::take_playlist(files, seconds)) start_playlist(std::move(files), seconds, info.t_ms);
  }
  if (playlist_active_ && playlist_request_id_ != 0) {
    const makapix::PlayStatus ps = makapix::play_status();
    if (ps.id == playlist_request_id_ && ps.state == makapix::PlayState::failed) {
      ESP_LOGW(TAG, "playlist: %s cannot be played (%s), skipping", ps.target.c_str(), ps.error.c_str());
      if (++playlist_failures_ >= playlist_.size()) {
        end_playlist("no file on the card plays");
        if (on_demand_) end_on_demand(info.t_ms, "playlist over");
        dirty = true;
      } else {
        playlist_request_next();
      }
    }
  }
  {
    makapix::Artwork art;
    uint32_t seconds = 0, id = 0;
    if (makapix::take_play(art, seconds, id)) {
      start_on_demand(std::move(art), seconds, id, info.t_ms);
      dirty = true;
    }
  }
  if (web::take_pattern()) {
    start_pattern(info.t_ms);
    dirty = true;
  }
  if (web::take_stop() && on_demand_) {
    end_on_demand(info.t_ms, "stopped from the web");
    dirty = true;
  }
  if (on_demand_) {
    if (!on_demand_indefinite_ && static_cast<int32_t>(info.t_ms - on_demand_until_ms_) >= 0) {
      if (playlist_active_) {
        on_demand_indefinite_ = true;  // keep this file up until the next one has loaded
        playlist_request_next();
      } else {
        end_on_demand(info.t_ms, "time is up");
        dirty = true;
      }
    }
  } else if (rotate(info.t_ms)) {
    dirty = true;
  }
  const bool due = kMaxSpeed || static_cast<int32_t>(info.t_ms - next_frame_ms_) >= 0;
  if (player_.is_open() && (dirty || (due && !single_frame_))) {
    if (decode_next(info.t_ms)) {
      scaler_.scale(player_.canvas(), frame.pixels());
    } else {
      ESP_LOGW(TAG, "%s: decode error %d after %" PRIu32 " frames, moving on", current_name_.c_str(),
               player_.last_error(), gif_frames_);
      if (on_demand_) {
        end_on_demand(info.t_ms, "decode error");
      } else {
        drop_current(info.t_ms);
      }
      frame.clear();
      if (player_.is_open() && decode_next(info.t_ms)) scaler_.scale(player_.canvas(), frame.pixels());
    }
    dirty = true;
  } else if (pattern_ && dirty) {
    draw_pattern(frame);
  } else if (!player_.is_open() && dirty) {
    frame.clear();
  }

  // The clock only changes once a minute; redraw the overlays when it does or when
  // a new GIF frame replaced them.
  char text[8];
  int h, m, s;
  if (clock::local_time(h, m, s)) {
    std::snprintf(text, sizeof(text), "%02d:%02d", h, m);
  } else {
    std::strcpy(text, "--:--");
  }
  const bool clock_changed = std::strcmp(text, clock_text_) != 0;
  if (clock_changed) std::strcpy(clock_text_, text);
  if (dirty || clock_changed || kShowFps) {
    if (!dirty && player_.is_open()) scaler_.scale(player_.canvas(), frame.pixels());  // restore under the old overlay
    if (!dirty && pattern_) draw_pattern(frame);
    draw_overlays(frame, info.fps);
    dirty = true;
  }
  return dirty;
}

// A web request arrived: play it for its time (0 = until the next request or /stop).
void GifShowScene::start_on_demand(makapix::Artwork &&art, uint32_t seconds, uint32_t id, uint32_t now_ms) {
  const bool from_playlist = playlist_active_ && id == playlist_request_id_;
  if (from_playlist) {
    playlist_request_id_ = 0;
    playlist_failures_ = 0;
  } else if (playlist_active_) {
    end_playlist("a new request replaced it");
  }
  log_gif_stats(now_ms);
  if (!on_demand_) rotation_ = std::move(download_);  // the rotation resumes it after the request
  on_demand_ = true;
  on_demand_id_ = id;
  on_demand_indefinite_ = seconds == 0;
  on_demand_until_ms_ = now_ms + seconds * 1000u;
  on_demand_until_us_ = seconds ? esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000 : 0;
  if (seconds == 0) {
    ESP_LOGI(TAG, "web request #%lu: playing until the next request", static_cast<unsigned long>(id));
  } else {
    ESP_LOGI(TAG, "web request #%lu: playing for %lu s", static_cast<unsigned long>(id),
             static_cast<unsigned long>(seconds));
  }
  if (!play_download(std::move(art), now_ms)) {
    makapix::set_play_error(id, "the file does not decode as a GIF");
    if (from_playlist) {
      ESP_LOGW(TAG, "playlist: %s does not decode as a GIF, skipping", playlist_[playlist_index_].c_str());
      if (++playlist_failures_ >= playlist_.size()) {
        end_playlist("no file on the card plays");
        end_on_demand(now_ms, "playlist over");
      } else {
        on_demand_indefinite_ = true;
        playlist_request_next();
      }
      return;
    }
    ESP_LOGW(TAG, "web request #%lu: the file does not decode as a GIF, back to the show",
             static_cast<unsigned long>(id));
    end_on_demand(now_ms, nullptr);
  }
}

// /sd/play: every GIF on the card in turn, each for `seconds`, looping until /stop or
// another request. Files are read by the fetcher task; the current one stays up until
// the next has loaded.
void GifShowScene::start_playlist(std::vector<std::string> &&files, uint32_t seconds, uint32_t now_ms) {
  (void) now_ms;
  playlist_ = std::move(files);
  playlist_seconds_ = seconds;
  playlist_active_ = !playlist_.empty();
  playlist_failures_ = 0;
  playlist_index_ = 0;
  if (!playlist_active_) return;
  ESP_LOGI(TAG, "card playlist: %u files, %lu s each, until /stop", static_cast<unsigned>(playlist_.size()),
           static_cast<unsigned long>(seconds));
  playlist_request(0);
}

void GifShowScene::playlist_request(size_t index) {
  playlist_index_ = index;
  makapix::PlayRequest r;
  r.file = playlist_[index];
  r.seconds = playlist_seconds_;
  playlist_request_id_ = makapix::request_play(r);
  if (playlist_request_id_ == 0) end_playlist("the fetcher task is not running");
}

void GifShowScene::playlist_request_next() { playlist_request((playlist_index_ + 1) % playlist_.size()); }

void GifShowScene::end_playlist(const char *why) {
  if (!playlist_active_) return;
  if (why) ESP_LOGI(TAG, "card playlist over (%s)", why);
  playlist_active_ = false;
  playlist_request_id_ = 0;
  playlist_.clear();
}

void GifShowScene::end_on_demand(uint32_t now_ms, const char *why) {
  end_playlist(nullptr);
  if (why && pattern_) {
    ESP_LOGI(TAG, "test pattern over (%s), back to the show", why);
  } else if (why) {
    ESP_LOGI(TAG, "web request #%lu over (%s), back to the show", static_cast<unsigned long>(on_demand_id_), why);
  }
  next_slot(now_ms);  // clears the on-demand state and plays the rotation's next artwork
}

void GifShowScene::publish_now_playing() const {
  web::NowPlaying now;
  now.name = current_name_;
  if (pattern_) {
    now.source = "pattern";
  } else if (!download_.file.empty()) {
    now.source = "sd";
  } else if (download_.gif.empty()) {
    now.source = "none";
  } else if (download_.sqid.empty()) {
    now.source = "url";
    now.url = download_.url;
  } else {
    now.source = "makapix";
    now.url = std::string("https://") + CONFIG_P64_MAKAPIX_HOST + "/p/" + download_.sqid;
  }
  now.width = player_.width();
  now.height = player_.height();
  now.bytes = current_bytes_;
  now.on_demand = on_demand_;
  now.started_us = esp_timer_get_time();
  now.until_us = on_demand_ ? on_demand_until_us_ : 0;
  now.playlist_index = playlist_active_ ? static_cast<int>(playlist_index_) : -1;
  now.playlist_count = playlist_active_ ? static_cast<int>(playlist_.size()) : 0;
  web::publish(now);
}

// /pattern: hold a synthetic tone test until /stop or the next request (it rides on the
// on-demand state with no time limit).
void GifShowScene::start_pattern(uint32_t now_ms) {
  log_gif_stats(now_ms);
  player_.close();
  if (!on_demand_) rotation_ = std::move(download_);
  download_ = makapix::Artwork{};
  end_playlist("test pattern requested");
  current_name_ = "test pattern";
  current_bytes_ = 0;
  gif_start_ms_ = now_ms;
  gif_frames_ = 0;
  single_frame_ = true;
  pattern_ = true;
  on_demand_ = true;
  on_demand_id_ = 0;
  on_demand_indefinite_ = true;
  on_demand_until_us_ = 0;
  ESP_LOGI(TAG, "showing the tone test pattern until /stop or the next request");
  publish_now_playing();
}

// Rows 0-7 black (the clock sits there). Rows 8-15: grey 0..63 by column, one code per
// column: the darkest quarter of the input range, where the panel's quantisation shows.
// Rows 16-23: grey 0..255 (4 per column). Rows 24-31 / 32-39 / 40-47: red, green and
// blue 0..63. Rows 48-63: a brown and a grey shaded sphere on dark grey, the kind of
// shape that looked flat on the panel at 8 bits.
void GifShowScene::draw_pattern(Frame &frame) const {
  frame.clear();
  for (int x = 0; x < kWidth; ++x) {
    const uint8_t q = static_cast<uint8_t>(x);            // 0..63
    const uint8_t f = static_cast<uint8_t>(x * 4 + 2);    // 2..254
    for (int y = 8; y < 16; ++y) frame.set(x, y, Rgb{q, q, q});
    for (int y = 16; y < 24; ++y) frame.set(x, y, Rgb{f, f, f});
    for (int y = 24; y < 32; ++y) frame.set(x, y, Rgb{q, 0, 0});
    for (int y = 32; y < 40; ++y) frame.set(x, y, Rgb{0, q, 0});
    for (int y = 40; y < 48; ++y) frame.set(x, y, Rgb{0, 0, q});
  }
  frame.fill_rect(0, 48, kWidth, 16, Rgb{24, 24, 24});
  struct Ball {
    float cx, cy, r;
    float base[3];
  };
  const Ball balls[] = {{16.0f, 56.0f, 7.0f, {150.0f, 95.0f, 55.0f}}, {48.0f, 56.0f, 7.0f, {110.0f, 110.0f, 110.0f}}};
  const float lx = -0.5f, ly = -0.6f, lz = 0.62f;  // light from the upper left, towards the viewer
  for (const Ball &b : balls) {
    for (int y = 48; y < 64; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        const float dx = (static_cast<float>(x) + 0.5f - b.cx) / b.r;
        const float dy = (static_cast<float>(y) + 0.5f - b.cy) / b.r;
        const float d2 = dx * dx + dy * dy;
        if (d2 > 1.0f) continue;
        const float nz = std::sqrt(1.0f - d2);
        const float lambert = std::max(0.0f, dx * lx + dy * ly + nz * lz);
        const float shade = 0.10f + 0.90f * lambert;
        frame.set(x, y, Rgb{static_cast<uint8_t>(std::min(255.0f, b.base[0] * shade)),
                            static_cast<uint8_t>(std::min(255.0f, b.base[1] * shade)),
                            static_cast<uint8_t>(std::min(255.0f, b.base[2] * shade))});
      }
    }
  }
}

void GifShowScene::draw_overlays(Frame &frame, float fps) const {
  draw_boxed_text(frame, 1, 1, clock_text_);
  if (kShowFps) {
    char text[8];
    std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(std::lround(std::clamp(fps, 0.0f, 999.0f))));
    draw_boxed_text(frame, kWidth - 1 - text_width_3x5(text), 1, text);
  }
}

}  // namespace p64

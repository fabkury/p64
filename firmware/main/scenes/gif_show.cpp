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
#include "gif_assets.hpp"
#include "net/clock.hpp"

namespace p64 {
namespace {

constexpr const char *TAG = "gif";
constexpr uint32_t kDwellMs = static_cast<uint32_t>(CONFIG_P64_GIF_DWELL_S) * 1000;
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

// Opacity of the box behind overlay text: half-transparent black, so the artwork stays
// visible through it while the digits keep their contrast.
constexpr uint8_t kBoxAlpha = 128;

// Draws text with its top-left at (x, y) on a half-transparent black box with 1 pixel
// of padding.
void draw_boxed_text(Frame &frame, int x, int y, const char *text) {
  const int w = text_width_3x5(text);
  frame.blend_rect(x - 1, y - 1, w + 2, 7, kBlack, kBoxAlpha);
  draw_text_3x5(frame, x, y, text, kText);
}

}  // namespace

const char *GifShowScene::name() const { return kMaxSpeed ? "gif playback, max speed" : "gif show"; }

void GifShowScene::enter(Display &display, Frame &frame) {
  ESP_LOGI(TAG, "%u GIFs embedded; %lu s per artwork, frame delays %s", static_cast<unsigned>(kGifAssetCount),
           static_cast<unsigned long>(kDwellMs / 1000), kMaxSpeed ? "ignored" : "honoured (min 100 ms when under 20)");
  display.set_brightness(max_brightness());
  embedded_index_ = kGifAssetCount ? esp_random() % kGifAssetCount : 0;
  play_embedded(0);
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
  if (!player_.open(data, size)) {
    ESP_LOGW(TAG, "%s: cannot open (AnimatedGIF error %d), skipped", current_name_.c_str(), player_.last_error());
    return false;
  }
  scaler_.configure(player_.width(), player_.height(), kWidth, kHeight);
  return true;
}

// Plays a random embedded GIF other than the last one shown. Tries a few if a file
// refuses to open; false only when none opens.
bool GifShowScene::play_embedded(uint32_t now_ms) {
  if (kGifAssetCount == 0) return false;
  download_ = makapix::Artwork{};  // release any downloaded bytes
  for (size_t tries = 0; tries < kGifAssetCount; ++tries) {
    size_t index = esp_random() % kGifAssetCount;
    if (kGifAssetCount > 1 && index == embedded_index_) index = (index + 1) % kGifAssetCount;
    embedded_index_ = index;
    const GifAsset &asset = kGifAssets[index];
    current_name_ = asset.name;
    if (open_current(asset.data, asset.size, now_ms)) {
      ESP_LOGI(TAG, "playing embedded %s (%dx%d, %zu bytes) at %dx%d", asset.name, player_.width(), player_.height(),
               asset.size, scaler_.out_w(), scaler_.out_h());
      return true;
    }
  }
  return false;
}

bool GifShowScene::play_download(makapix::Artwork &&art, uint32_t now_ms) {
  player_.close();  // it may still point into the previous download's bytes
  download_ = std::move(art);
  current_name_ = "makapix " + download_.sqid;
  if (!open_current(download_.gif.data(), download_.gif.size(), now_ms)) return false;
  ESP_LOGI(TAG, "playing makapix %s \"%s\" by %s (%dx%d, %u bytes) at %dx%d, https://%s/p/%s", download_.sqid.c_str(),
           download_.title.c_str(), download_.artist.c_str(), player_.width(), player_.height(),
           static_cast<unsigned>(download_.gif.size()), scaler_.out_w(), scaler_.out_h(), CONFIG_P64_MAKAPIX_HOST,
           download_.sqid.c_str());
  return true;
}

// End of a 30 s slot: the downloaded artwork if one is waiting, else an embedded GIF;
// then ask for the next download so it is ready when this slot ends.
void GifShowScene::next_slot(uint32_t now_ms) {
  log_gif_stats(now_ms);
  makapix::Artwork art;
  bool playing = false;
  if (makapix::take_ready(art)) playing = play_download(std::move(art), now_ms);
  if (!playing) {
    ESP_LOGI(TAG, "no downloaded artwork ready, using an embedded GIF");
    playing = play_embedded(now_ms);
  }
  if (!playing) player_.close();
  makapix::request_next();
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

  if (info.t_ms - gif_start_ms_ >= kDwellMs) {
    next_slot(info.t_ms);
    dirty = true;
  }
  const bool due = kMaxSpeed || static_cast<int32_t>(info.t_ms - next_frame_ms_) >= 0;
  if (player_.is_open() && (dirty || (due && !single_frame_))) {
    if (decode_next(info.t_ms)) {
      scaler_.scale(player_.canvas(), frame.pixels());
    } else {
      ESP_LOGW(TAG, "%s: decode error %d after %" PRIu32 " frames, moving on", current_name_.c_str(),
               player_.last_error(), gif_frames_);
      if (!play_embedded(info.t_ms)) player_.close();
      frame.clear();
      if (player_.is_open() && decode_next(info.t_ms)) scaler_.scale(player_.canvas(), frame.pixels());
    }
    dirty = true;
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
    draw_overlays(frame, info.fps);
    dirty = true;
  }
  return dirty;
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

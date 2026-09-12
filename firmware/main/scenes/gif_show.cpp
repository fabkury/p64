#include "scenes/gif_show.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>

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

// Draws text with its top-left at (x, y) on a black box with 1 pixel of padding.
void draw_boxed_text(Frame &frame, int x, int y, const char *text) {
  const int w = text_width_3x5(text);
  frame.fill_rect(x - 1, y - 1, w + 2, 7, kBlack);
  draw_text_3x5(frame, x, y, text, kText);
}

}  // namespace

const char *GifShowScene::name() const { return kMaxSpeed ? "gif playback, max speed" : "gif show"; }

void GifShowScene::enter(Display &display, Frame &frame) {
  order_.resize(kGifAssetCount);
  std::iota(order_.begin(), order_.end(), size_t{0});
  std::minstd_rand rng(esp_random());
  std::shuffle(order_.begin(), order_.end(), rng);
  ESP_LOGI(TAG, "%u GIFs embedded, shuffled; %lu s each, frame delays %s", static_cast<unsigned>(kGifAssetCount),
           static_cast<unsigned long>(kDwellMs / 1000), kMaxSpeed ? "ignored" : "honoured (min 100 ms when under 20)");

  display.set_brightness(max_brightness());
  position_ = 0;
  if (!start_gif(position_, 0)) advance(0);
  frame.clear();
  decode_next(0);
  scaler_.scale(player_.canvas(), frame.pixels());
  draw_overlays(frame, 0.0f);
  display.present(frame);
}

bool GifShowScene::start_gif(size_t order_index, uint32_t now_ms) {
  if (order_.empty()) return false;
  const GifAsset &asset = kGifAssets[order_[order_index]];
  gif_start_ms_ = now_ms;
  gif_frames_ = 0;
  gif_decode_us_ = 0;
  next_frame_ms_ = now_ms;
  single_frame_ = false;
  if (!player_.open(asset.data, asset.size)) {
    ESP_LOGW(TAG, "%s: cannot open (AnimatedGIF error %d), skipped", asset.name, player_.last_error());
    return false;
  }
  scaler_.configure(player_.width(), player_.height(), kWidth, kHeight);
  ESP_LOGI(TAG, "playing %s (%dx%d, %zu bytes) at %dx%d", asset.name, player_.width(), player_.height(), asset.size,
           scaler_.out_w(), scaler_.out_h());
  return true;
}

void GifShowScene::advance(uint32_t now_ms) {
  // Move to the next GIF that opens; give up after one full pass so a folder of bad
  // files cannot spin forever.
  for (size_t tries = 0; tries < order_.size(); ++tries) {
    position_ = (position_ + 1) % order_.size();
    if (start_gif(position_, now_ms)) return;
  }
}

void GifShowScene::log_gif_stats(uint32_t now_ms) const {
  if (!player_.is_open() || gif_frames_ == 0) return;
  const GifAsset &asset = kGifAssets[order_[position_]];
  const float seconds = static_cast<float>(now_ms - gif_start_ms_) / 1000.0f;
  ESP_LOGI(TAG, "%s: %" PRIu32 " frames in %.1f s = %.1f fps, %" PRIu32 " loops, decode+scale %.3f ms/frame",
           asset.name, gif_frames_, seconds, static_cast<float>(gif_frames_) / seconds, player_.loops(),
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

  if (!order_.empty()) {
    if (info.t_ms - gif_start_ms_ >= kDwellMs) {
      log_gif_stats(info.t_ms);
      advance(info.t_ms);
      dirty = true;
    }
    const bool due = kMaxSpeed || static_cast<int32_t>(info.t_ms - next_frame_ms_) >= 0;
    if (player_.is_open() && (dirty || (due && !single_frame_))) {
      if (decode_next(info.t_ms)) {
        scaler_.scale(player_.canvas(), frame.pixels());
      } else {
        ESP_LOGW(TAG, "%s: decode error %d after %" PRIu32 " frames, skipping",
                 kGifAssets[order_[position_]].name, player_.last_error(), gif_frames_);
        advance(info.t_ms);
        frame.clear();
      }
      dirty = true;
    } else if (!player_.is_open()) {
      frame.clear();
      dirty = true;
    }
  } else if (info.t_ms == 0) {
    frame.clear();
    dirty = true;
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

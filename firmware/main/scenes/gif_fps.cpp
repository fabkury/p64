#include "scenes/gif_fps.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <numeric>
#include <random>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "font3x5.hpp"
#include "gif_assets.hpp"

namespace p64 {
namespace {

constexpr const char *TAG = "gif";
constexpr uint32_t kPerGifMs = 10 * 1000;
constexpr Rgb kCounter{255, 255, 255};

}  // namespace

void GifFpsScene::enter(Display &display, Frame &frame) {
  order_.resize(kGifAssetCount);
  std::iota(order_.begin(), order_.end(), size_t{0});
  std::minstd_rand rng(esp_random());
  std::shuffle(order_.begin(), order_.end(), rng);
  ESP_LOGI(TAG, "%u GIFs embedded, shuffled; 10 s each, frame delays ignored", static_cast<unsigned>(kGifAssetCount));

  display.set_brightness(max_brightness());
  position_ = 0;
  if (!start_gif(position_, 0)) advance(0);
  render(display, frame, FrameInfo{0, 0.0f, 0.0f});
  display.present(frame);
}

bool GifFpsScene::start_gif(size_t order_index, uint32_t now_ms) {
  if (order_.empty()) return false;
  const GifAsset &asset = kGifAssets[order_[order_index]];
  gif_start_ms_ = now_ms;
  gif_frames_ = 0;
  gif_decode_us_ = 0;
  if (!player_.open(asset.data, asset.size)) {
    ESP_LOGW(TAG, "%s: cannot open (AnimatedGIF error %d), skipped", asset.name, player_.last_error());
    return false;
  }
  scaler_.configure(player_.width(), player_.height(), kWidth, kHeight);
  ESP_LOGI(TAG, "playing %s (%dx%d, %zu bytes) at %dx%d", asset.name, player_.width(), player_.height(), asset.size,
           scaler_.out_w(), scaler_.out_h());
  return true;
}

void GifFpsScene::advance(uint32_t now_ms) {
  // Move to the next GIF that opens; give up after one full pass so a folder of bad
  // files cannot spin forever.
  for (size_t tries = 0; tries < order_.size(); ++tries) {
    position_ = (position_ + 1) % order_.size();
    if (start_gif(position_, now_ms)) return;
  }
}

void GifFpsScene::log_gif_stats(uint32_t now_ms) const {
  if (!player_.is_open() || gif_frames_ == 0) return;
  const GifAsset &asset = kGifAssets[order_[position_]];
  const float seconds = static_cast<float>(now_ms - gif_start_ms_) / 1000.0f;
  ESP_LOGI(TAG, "%s: %" PRIu32 " frames in %.1f s = %.1f fps, %" PRIu32 " loops, decode+scale %.3f ms/frame",
           asset.name, gif_frames_, seconds, static_cast<float>(gif_frames_) / seconds, player_.loops(),
           static_cast<float>(gif_decode_us_) / static_cast<float>(gif_frames_) / 1000.0f);
}

bool GifFpsScene::render(Display &, Frame &frame, const FrameInfo &info) {
  if (order_.empty()) {
    frame.clear();
    draw_counter(frame, info.fps);
    return true;
  }
  if (info.t_ms - gif_start_ms_ >= kPerGifMs) {
    log_gif_stats(info.t_ms);
    advance(info.t_ms);
  }
  if (player_.is_open()) {
    const int64_t t0 = esp_timer_get_time();
    if (player_.next_frame()) {
      scaler_.scale(player_.canvas(), frame.pixels());
      gif_decode_us_ += static_cast<uint64_t>(esp_timer_get_time() - t0);
      ++gif_frames_;
    } else {
      ESP_LOGW(TAG, "%s: decode error %d after %" PRIu32 " frames, skipping", kGifAssets[order_[position_]].name,
               player_.last_error(), gif_frames_);
      advance(info.t_ms);
      frame.clear();
    }
  } else {
    frame.clear();
  }
  draw_counter(frame, info.fps);
  return true;
}

void GifFpsScene::draw_counter(Frame &frame, float fps) const {
  const unsigned value = static_cast<unsigned>(std::lround(std::clamp(fps, 0.0f, 999.0f)));
  int digits = 1;
  for (unsigned v = value; v >= 10; v /= 10) ++digits;
  // Digits are 3 wide with 1-pixel gaps, drawn right-aligned at column kWidth-2 on row 1;
  // the black box adds one pixel of padding on every side.
  const int box_w = digits * 4 + 1;
  frame.fill_rect(kWidth - box_w, 0, box_w, 7, kBlack);
  draw_number_right(frame, kWidth - 2, 1, value, kCounter);
}

}  // namespace p64

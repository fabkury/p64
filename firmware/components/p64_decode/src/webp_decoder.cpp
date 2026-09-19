#include "p64/decode/webp_decoder.hpp"

#include <cstring>

#include "p64/decode/alpha.hpp"
#include "webp/decode.h"
#include "webp/demux.h"

namespace p64::decode {
namespace {

constexpr uint32_t kStaticDelayMs = 100;

}  // namespace

WebPDecoder::~WebPDecoder() { close(); }

void WebPDecoder::fail(const char *why) {
  error_ = why;
  open_ = false;
}

void WebPDecoder::close() {
  if (anim_) {
    WebPAnimDecoderDelete(anim_);
    anim_ = nullptr;
  }
  open_ = false;
  canvas_rgb_.clear();
  canvas_rgb_.shrink_to_fit();
  static_pixels_.clear();
  static_pixels_.shrink_to_fit();
}

bool WebPDecoder::open(const uint8_t *data, size_t size, gfx::Rgb background) {
  close();
  background_ = background;
  error_ = "";
  info_ = Info{};
  WebPBitstreamFeatures features;
  if (!data || WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
    error_ = "not a decodable WebP";
    return false;
  }
  if (features.width <= 0 || features.height <= 0 || features.width > 16384 || features.height > 16384) {
    error_ = "bad WebP dimensions";
    return false;
  }
  info_.format = Format::WebP;
  info_.width = features.width;
  info_.height = features.height;
  info_.has_alpha = features.has_alpha != 0;
  info_.animated = features.has_animation != 0;
  const size_t pixels = static_cast<size_t>(features.width) * features.height;
  canvas_rgb_.assign(pixels * 3, 0);

  if (info_.animated) {
    WebPAnimDecoderOptions opts;
    if (!WebPAnimDecoderOptionsInit(&opts)) {
      error_ = "libwebp options";
      return false;
    }
    // WebPAnimDecoder only offers RGBA-based modes; premultiplied saves a multiply per
    // channel when flattening.
    opts.color_mode = MODE_rgbA;
    opts.use_threads = 0;
    WebPData wd;
    wd.bytes = data;
    wd.size = size;
    anim_ = WebPAnimDecoderNew(&wd, &opts);
    if (!anim_) {
      error_ = "WebP animation cannot be decoded";
      return false;
    }
    WebPAnimInfo ainfo;
    if (!WebPAnimDecoderGetInfo(anim_, &ainfo) || ainfo.frame_count == 0 || ainfo.canvas_width == 0 ||
        ainfo.canvas_height == 0) {
      error_ = "bad WebP animation metadata";
      close();
      return false;
    }
    info_.width = static_cast<int>(ainfo.canvas_width);
    info_.height = static_cast<int>(ainfo.canvas_height);
    info_.frame_count = ainfo.frame_count;
    canvas_rgb_.assign(static_cast<size_t>(info_.width) * info_.height * 3, 0);
    if (ainfo.frame_count == 1) info_.animated = false;
    last_timestamp_ms_ = 0;
  } else {
    info_.frame_count = 1;
    static_has_alpha_ = info_.has_alpha;
    if (static_has_alpha_) {
      static_pixels_.assign(pixels * 4, 0);
      if (!WebPDecodeRGBAInto(data, size, static_pixels_.data(), static_pixels_.size(), features.width * 4)) {
        error_ = "WebP image cannot be decoded";
        return false;
      }
    } else {
      static_pixels_.assign(pixels * 3, 0);
      if (!WebPDecodeRGBInto(data, size, static_pixels_.data(), static_pixels_.size(), features.width * 3)) {
        error_ = "WebP image cannot be decoded";
        return false;
      }
    }
  }
  frames_emitted_ = 0;
  at_end_ = false;
  open_ = true;
  return true;
}

void WebPDecoder::rewind() {
  if (!open_) return;
  if (anim_) WebPAnimDecoderReset(anim_);
  last_timestamp_ms_ = 0;
  frames_emitted_ = 0;
  at_end_ = false;
}

bool WebPDecoder::next(uint32_t &delay_ms) {
  if (!open_) return false;
  const size_t pixels = static_cast<size_t>(info_.width) * info_.height;
  if (!anim_) {
    if (static_has_alpha_) {
      flatten_rgba(static_pixels_.data(), canvas_rgb_.data(), pixels, background_);
    } else {
      std::memcpy(canvas_rgb_.data(), static_pixels_.data(), pixels * 3);
    }
    delay_ms = kStaticDelayMs;
    at_end_ = true;
    return true;
  }
  if (at_end_) rewind();
  uint8_t *frame = nullptr;
  int timestamp_ms = 0;
  if (!WebPAnimDecoderGetNext(anim_, &frame, &timestamp_ms) || !frame) {
    fail("corrupt WebP frame");
    return false;
  }
  int stored = timestamp_ms - last_timestamp_ms_;
  if (stored < 0) stored = 0;
  last_timestamp_ms_ = timestamp_ms;
  if (info_.has_alpha) {
    flatten_premultiplied(frame, canvas_rgb_.data(), pixels, background_);
  } else {
    const uint8_t *s = frame;
    uint8_t *d = canvas_rgb_.data();
    for (size_t i = 0; i < pixels; ++i, s += 4, d += 3) {
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
    }
  }
  ++frames_emitted_;
  at_end_ = !WebPAnimDecoderHasMoreFrames(anim_);
  delay_ms = browser_delay_ms(Format::WebP, static_cast<uint32_t>(stored));
  return true;
}

}  // namespace p64::decode

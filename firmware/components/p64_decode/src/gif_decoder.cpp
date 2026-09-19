#include "p64/decode/gif_decoder.hpp"

#include <algorithm>
#include <cstring>

namespace p64::decode {

GifDecoder::GifDecoder() = default;
GifDecoder::~GifDecoder() { close(); }

bool GifDecoder::open(const uint8_t *data, size_t size, gfx::Rgb background) {
  close();
  background_ = background;
  if (!data || size < 13) {
    error_ = "file too short";
    return false;
  }
  if (!gif_) gif_ = std::make_unique<AnimatedGIF>();
  gif_->begin(GIF_PALETTE_RGB888);
  gif_->setDrawType(GIF_DRAW_RAW);
  // The library only reads from the buffer; its API just lacks const.
  if (!gif_->open(const_cast<uint8_t *>(data), static_cast<int>(size), &GifDecoder::draw_callback)) {
    error_ = "not a decodable GIF";
    return false;
  }
  const int w = gif_->getCanvasWidth(), h = gif_->getCanvasHeight();
  if (w <= 0 || h <= 0) {
    error_ = "bad canvas size";
    gif_->close();
    return false;
  }
  info_ = Info{};
  info_.format = Format::Gif;
  info_.width = w;
  info_.height = h;
  info_.animated = true;  // corrected after the first loop when it turns out to be one frame
  const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
  canvas_.reset(new uint8_t[bytes]);
  fill_background(0, 0, w, h);
  backup_.reset();
  previous_ = Rect{};
  current_ = Rect{};
  frames_ = loops_ = frames_this_loop_ = 0;
  at_end_ = false;
  error_ = "";
  open_ = true;
  return true;
}

void GifDecoder::close() {
  if (gif_ && open_) gif_->close();
  open_ = false;
  canvas_.reset();
  backup_.reset();
}

void GifDecoder::fail(const char *why) {
  error_ = why;
  open_ = false;
}

void GifDecoder::rewind() {
  if (!open_) return;
  gif_->reset();
  fill_background(0, 0, info_.width, info_.height);
  previous_ = Rect{};
  at_end_ = false;
  ++loops_;
  frames_this_loop_ = 0;
}

bool GifDecoder::next(uint32_t &delay_ms) {
  if (!open_) return false;
  if (at_end_) rewind();
  frame_started_ = false;
  int stored_ms = 0;
  int rc = gif_->playFrame(false, &stored_ms, this);
  if (!frame_started_) {
    // Nothing was drawn: trailing data after the last frame (the frame returned before
    // this call was the loop's last one, unannounced), or a broken file.
    if (rc < 0) {
      fail("decode error");
      return false;
    }
    if (loops_ == 0) {
      info_.frame_count = frames_this_loop_;
      if (frames_this_loop_ == 1) info_.animated = false;
    }
    rewind();
    frame_started_ = false;
    rc = gif_->playFrame(false, &stored_ms, this);
    if (!frame_started_) {
      fail("no frame could be decoded");
      return false;
    }
  }
  previous_ = current_;
  ++frames_;
  ++frames_this_loop_;
  at_end_ = (rc <= 0);
  if (at_end_ && loops_ == 0 && frames_this_loop_ == 1) info_.animated = false;
  if (at_end_ && loops_ == 0) info_.frame_count = frames_this_loop_;
  delay_ms = browser_delay_ms(Format::Gif, static_cast<uint32_t>(std::max(stored_ms, 0)));
  return true;
}

void GifDecoder::draw_callback(GIFDRAW *d) { static_cast<GifDecoder *>(d->pUser)->on_line(d); }

void GifDecoder::begin_frame(const GIFDRAW *d) {
  dispose_previous();
  current_ = Rect{d->iX, d->iY, d->iWidth, d->iHeight, d->ucDisposalMethod};
  if (d->ucHasTransparency) info_.has_alpha = true;
  if (current_.disposal == 3) {
    const size_t bytes = static_cast<size_t>(info_.width) * static_cast<size_t>(info_.height) * 3;
    if (!backup_) backup_.reset(new uint8_t[bytes]);
    std::memcpy(backup_.get(), canvas_.get(), bytes);
  }
}

void GifDecoder::fill_background(int x, int y, int w, int h) {
  const int x0 = std::max(x, 0), y0 = std::max(y, 0);
  const int x1 = std::min(x + w, info_.width), y1 = std::min(y + h, info_.height);
  if (x1 <= x0) return;
  for (int yy = y0; yy < y1; ++yy) {
    uint8_t *p = canvas_.get() + (static_cast<size_t>(yy) * info_.width + x0) * 3;
    if (background_.r == background_.g && background_.g == background_.b) {
      std::memset(p, background_.r, static_cast<size_t>(x1 - x0) * 3);
      continue;
    }
    for (int xx = x0; xx < x1; ++xx, p += 3) {
      p[0] = background_.r;
      p[1] = background_.g;
      p[2] = background_.b;
    }
  }
}

void GifDecoder::dispose_previous() {
  if (previous_.disposal == 2) {
    fill_background(previous_.x, previous_.y, previous_.w, previous_.h);
  } else if (previous_.disposal == 3 && backup_) {
    const size_t bytes = static_cast<size_t>(info_.width) * static_cast<size_t>(info_.height) * 3;
    std::memcpy(canvas_.get(), backup_.get(), bytes);
  }
}

void GifDecoder::on_line(const GIFDRAW *d) {
  if (!frame_started_) {
    frame_started_ = true;
    begin_frame(d);
  }
  const int y = d->iY + d->y;
  if (y < 0 || y >= info_.height) return;
  const int x0 = std::max(d->iX, 0);
  const int x1 = std::min(d->iX + d->iWidth, info_.width);
  if (x1 <= x0) return;
  const uint8_t *src = d->pPixels + (x0 - d->iX);
  const uint8_t *pal = d->pPalette24;
  uint8_t *dst = canvas_.get() + (static_cast<size_t>(y) * info_.width + x0) * 3;
  const int n = x1 - x0;
  if (d->ucHasTransparency) {
    const uint8_t t = d->ucTransparent;
    for (int i = 0; i < n; ++i, dst += 3) {
      const uint8_t idx = src[i];
      if (idx == t) continue;  // transparent: the canvas keeps what was there
      const uint8_t *c = pal + idx * 3;
      dst[0] = c[0];
      dst[1] = c[1];
      dst[2] = c[2];
    }
  } else {
    for (int i = 0; i < n; ++i, dst += 3) {
      const uint8_t *c = pal + src[i] * 3;
      dst[0] = c[0];
      dst[1] = c[1];
      dst[2] = c[2];
    }
  }
}

}  // namespace p64::decode

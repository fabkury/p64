#include "gif_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace p64 {

// ---------------------------------------------------------------------------
// GifPlayer
// ---------------------------------------------------------------------------

GifPlayer::GifPlayer() = default;
GifPlayer::~GifPlayer() { close(); }

bool GifPlayer::open(const uint8_t *data, size_t size) {
  close();
  if (!data || size < 13) return false;
  if (!gif_) gif_ = std::make_unique<AnimatedGIF>();
  gif_->begin(GIF_PALETTE_RGB888);
  gif_->setDrawType(GIF_DRAW_RAW);
  // The library only reads from the buffer; its API just lacks const.
  if (!gif_->open(const_cast<uint8_t *>(data), static_cast<int>(size), &GifPlayer::draw_callback)) {
    last_error_ = gif_->getLastError();
    return false;
  }
  w_ = gif_->getCanvasWidth();
  h_ = gif_->getCanvasHeight();
  if (w_ <= 0 || h_ <= 0) {
    last_error_ = GIF_BAD_FILE;
    return false;
  }
  const size_t bytes = static_cast<size_t>(w_) * static_cast<size_t>(h_) * 3;
  canvas_.reset(new uint8_t[bytes]());
  backup_.reset();
  previous_ = Rect{};
  current_ = Rect{};
  frames_ = 0;
  loops_ = 0;
  at_end_ = false;
  last_error_ = GIF_SUCCESS;
  open_ = true;
  return true;
}

void GifPlayer::close() {
  if (gif_ && open_) gif_->close();
  open_ = false;
  canvas_.reset();
  backup_.reset();
  w_ = h_ = 0;
}

void GifPlayer::fail() {
  last_error_ = gif_ ? gif_->getLastError() : GIF_BAD_FILE;
  if (last_error_ == GIF_SUCCESS) last_error_ = GIF_DECODE_ERROR;
  open_ = false;
}

void GifPlayer::rewind() {
  gif_->reset();
  std::memset(canvas_.get(), 0, static_cast<size_t>(w_) * static_cast<size_t>(h_) * 3);
  previous_ = Rect{};
  at_end_ = false;
  ++loops_;
}

bool GifPlayer::next_frame() {
  if (!open_) return false;
  if (at_end_) rewind();
  frame_started_ = false;
  int delay_ms = 0;
  int rc = gif_->playFrame(false, &delay_ms, this);
  if (!frame_started_) {
    // Nothing was drawn: either trailing data after the last frame, or a broken file.
    if (rc < 0) {
      fail();
      return false;
    }
    rewind();
    frame_started_ = false;
    rc = gif_->playFrame(false, &delay_ms, this);
    if (!frame_started_) {
      fail();
      return false;
    }
  }
  previous_ = current_;
  ++frames_;
  last_delay_ms_ = delay_ms;
  at_end_ = (rc <= 0);
  return true;
}

void GifPlayer::draw_callback(GIFDRAW *d) { static_cast<GifPlayer *>(d->pUser)->on_line(d); }

void GifPlayer::begin_frame(const GIFDRAW *d) {
  dispose_previous();
  current_ = Rect{d->iX, d->iY, d->iWidth, d->iHeight, d->ucDisposalMethod};
  if (current_.disposal == 3) {
    const size_t bytes = static_cast<size_t>(w_) * static_cast<size_t>(h_) * 3;
    if (!backup_) backup_.reset(new uint8_t[bytes]);
    std::memcpy(backup_.get(), canvas_.get(), bytes);
  }
}

void GifPlayer::dispose_previous() {
  const size_t bytes = static_cast<size_t>(w_) * static_cast<size_t>(h_) * 3;
  if (previous_.disposal == 2) {
    const int x0 = std::max(previous_.x, 0);
    const int y0 = std::max(previous_.y, 0);
    const int x1 = std::min(previous_.x + previous_.w, w_);
    const int y1 = std::min(previous_.y + previous_.h, h_);
    for (int y = y0; y < y1; ++y) {
      if (x1 > x0) std::memset(canvas_.get() + (static_cast<size_t>(y) * w_ + x0) * 3, 0, (x1 - x0) * 3);
    }
  } else if (previous_.disposal == 3 && backup_) {
    std::memcpy(canvas_.get(), backup_.get(), bytes);
  }
}

void GifPlayer::on_line(const GIFDRAW *d) {
  if (!frame_started_) {
    frame_started_ = true;
    begin_frame(d);
  }
  const int y = d->iY + d->y;
  if (y < 0 || y >= h_) return;
  const int x0 = std::max(d->iX, 0);
  const int x1 = std::min(d->iX + d->iWidth, w_);
  if (x1 <= x0) return;
  const uint8_t *src = d->pPixels + (x0 - d->iX);
  const uint8_t *pal = d->pPalette24;
  uint8_t *dst = canvas_.get() + (static_cast<size_t>(y) * w_ + x0) * 3;
  const int n = x1 - x0;
  if (d->ucHasTransparency) {
    const uint8_t t = d->ucTransparent;
    for (int i = 0; i < n; ++i, dst += 3) {
      const uint8_t idx = src[i];
      if (idx == t) continue;
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

// ---------------------------------------------------------------------------
// Scaler
// ---------------------------------------------------------------------------

void Scaler::configure(int src_w, int src_h, int dst_w, int dst_h) {
  src_w_ = src_w;
  src_h_ = src_h;
  dst_w_ = dst_w;
  dst_h_ = dst_h;
  const double s = std::min(static_cast<double>(dst_w) / src_w, static_cast<double>(dst_h) / src_h);
  ow_ = std::clamp(static_cast<int>(std::lround(src_w * s)), 1, dst_w);
  oh_ = std::clamp(static_cast<int>(std::lround(src_h * s)), 1, dst_h);
  ox_ = (dst_w - ow_) / 2;
  oy_ = (dst_h - oh_) / 2;

  auto spans = [](int src, int out) {
    std::vector<Span> v(static_cast<size_t>(out));
    for (int i = 0; i < out; ++i) {
      if (out >= src) {  // enlarging: nearest source pixel
        const int p = std::min(src - 1, static_cast<int>((static_cast<int64_t>(i) * 2 + 1) * src / (2 * out)));
        v[i] = Span{static_cast<uint16_t>(p), static_cast<uint16_t>(p + 1)};
      } else {  // shrinking: box of source pixels, boxes tile the source exactly
        const int b = static_cast<int>(static_cast<int64_t>(i) * src / out);
        int e = static_cast<int>(static_cast<int64_t>(i + 1) * src / out);
        if (e <= b) e = b + 1;
        v[i] = Span{static_cast<uint16_t>(b), static_cast<uint16_t>(std::min(e, src))};
      }
    }
    return v;
  };
  cols_ = spans(src_w, ow_);
  rows_ = spans(src_h, oh_);
}

void Scaler::scale(const uint8_t *src, uint8_t *dst) const {
  std::memset(dst, 0, static_cast<size_t>(dst_w_) * dst_h_ * 3);
  for (int r = 0; r < oh_; ++r) {
    const Span ys = rows_[static_cast<size_t>(r)];
    uint8_t *out = dst + (static_cast<size_t>(oy_ + r) * dst_w_ + ox_) * 3;
    for (int c = 0; c < ow_; ++c, out += 3) {
      const Span xs = cols_[static_cast<size_t>(c)];
      const int count = (ys.end - ys.begin) * (xs.end - xs.begin);
      if (count == 1) {
        const uint8_t *p = src + (static_cast<size_t>(ys.begin) * src_w_ + xs.begin) * 3;
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        continue;
      }
      uint32_t sr = 0, sg = 0, sb = 0;
      for (int y = ys.begin; y < ys.end; ++y) {
        const uint8_t *p = src + (static_cast<size_t>(y) * src_w_ + xs.begin) * 3;
        for (int x = xs.begin; x < xs.end; ++x, p += 3) {
          sr += p[0];
          sg += p[1];
          sb += p[2];
        }
      }
      const uint32_t half = static_cast<uint32_t>(count) / 2;
      out[0] = static_cast<uint8_t>((sr + half) / count);
      out[1] = static_cast<uint8_t>((sg + half) / count);
      out[2] = static_cast<uint8_t>((sb + half) / count);
    }
  }
}

}  // namespace p64

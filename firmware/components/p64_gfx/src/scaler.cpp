#include "p64/gfx/scaler.hpp"

#include <algorithm>
#include <cstring>

namespace p64::gfx {

void Scaler::configure(int src_w, int src_h, int dst_w, int dst_h) {
  src_w_ = std::max(src_w, 1);
  src_h_ = std::max(src_h, 1);
  dst_w_ = std::max(dst_w, 1);
  dst_h_ = std::max(dst_h, 1);
  if (src_w_ <= dst_w_ && src_h_ <= dst_h_) {
    // Enlarging (or same size): the largest integer factor that fits both axes, so
    // every source pixel becomes an exact factor x factor block.
    factor_ = std::max(1, std::min(dst_w_ / src_w_, dst_h_ / src_h_));
    ow_ = src_w_ * factor_;
    oh_ = src_h_ * factor_;
  } else {
    // Shrinking on at least one axis: fit by the tighter ratio, non-integer allowed.
    factor_ = 1;
    const double s = std::min(static_cast<double>(dst_w_) / src_w_, static_cast<double>(dst_h_) / src_h_);
    ow_ = std::clamp(static_cast<int>(src_w_ * s + 0.5), 1, dst_w_);
    oh_ = std::clamp(static_cast<int>(src_h_ * s + 0.5), 1, dst_h_);
  }
  ox_ = (dst_w_ - ow_) / 2;
  oy_ = (dst_h_ - oh_) / 2;

  auto spans = [](int src, int out) {
    std::vector<Span> v(static_cast<size_t>(out));
    for (int i = 0; i < out; ++i) {
      if (out >= src) {  // enlarging: the source pixel whose block this column lies in
        const int p = std::min(src - 1, static_cast<int>(static_cast<int64_t>(i) * src / out));
        v[i] = Span{static_cast<uint16_t>(p), static_cast<uint16_t>(p + 1)};
      } else {  // shrinking: a box of source pixels; boxes tile the source exactly
        const int b = static_cast<int>(static_cast<int64_t>(i) * src / out);
        int e = static_cast<int>(static_cast<int64_t>(i + 1) * src / out);
        if (e <= b) e = b + 1;
        v[i] = Span{static_cast<uint16_t>(b), static_cast<uint16_t>(std::min(e, src))};
      }
    }
    return v;
  };
  cols_ = spans(src_w_, ow_);
  rows_ = spans(src_h_, oh_);
}

void Scaler::scale(const uint8_t *src, uint8_t *dst, Rgb bars) const {
  // Bars first (only the rows and columns outside the picture need it, but the whole
  // frame is 12 KB and a memset-style fill is cheaper than bookkeeping).
  if (bars.r == bars.g && bars.g == bars.b) {
    std::memset(dst, bars.r, static_cast<size_t>(dst_w_) * dst_h_ * 3);
  } else {
    uint8_t *p = dst;
    for (int i = 0; i < dst_w_ * dst_h_; ++i, p += 3) {
      p[0] = bars.r;
      p[1] = bars.g;
      p[2] = bars.b;
    }
  }
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

}  // namespace p64::gfx

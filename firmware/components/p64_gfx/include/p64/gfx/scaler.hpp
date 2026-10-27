// p64 -- Scaler: fits an RGB888 canvas of one size into a destination of another size,
// keeping the aspect ratio: nearest neighbour by an integer factor when enlarging, box
// average by a power of two when shrinking (never a fractional ratio, so the pixel grid
// survives; an odd remainder goes into the edge pixel), bars in the background colour
// around the picture (spec section 3.5).
#pragma once

#include <cstdint>
#include <vector>

#include "p64/gfx/frame.hpp"

namespace p64::gfx {

class Scaler {
 public:
  // Prepares the column and row spans for src -> dst. Enlarging uses the largest
  // integer factor that fits (crisp pixels); shrinking the fewest halvings that fit,
  // averaging 2^k x 2^k boxes.
  void configure(int src_w, int src_h, int dst_w, int dst_h);
  // dst holds dst_w*dst_h*3 bytes; the area outside the picture is filled with `bars`.
  void scale(const uint8_t *src, uint8_t *dst, Rgb bars = kBlack) const;
  // Convenience for the panel: dst is a logical Frame.
  void scale(const uint8_t *src, Frame &dst, Rgb bars = kBlack) const { scale(src, dst.pixels(), bars); }

  int src_width() const { return src_w_; }
  int src_height() const { return src_h_; }
  int out_x() const { return ox_; }
  int out_y() const { return oy_; }
  int out_w() const { return ow_; }
  int out_h() const { return oh_; }
  bool enlarging() const { return factor_ > 1; }
  int factor() const { return factor_; }  // integer enlargement factor, 1 when shrinking or same size
  int halvings() const { return halvings_; }  // k of the 1/2^k shrink, 0 when not shrinking

 private:
  struct Span {
    uint16_t begin, end;  // source pixel range [begin, end) for one destination column/row
  };
  std::vector<Span> cols_, rows_;
  int src_w_ = 0, src_h_ = 0, dst_w_ = 0, dst_h_ = 0;
  int ox_ = 0, oy_ = 0, ow_ = 0, oh_ = 0;
  int factor_ = 1;
  int halvings_ = 0;
};

}  // namespace p64::gfx

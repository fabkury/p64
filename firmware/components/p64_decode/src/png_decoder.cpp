#include "p64/decode/png_decoder.hpp"

#include <csetjmp>
#include <cstring>

#include "p64/decode/alpha.hpp"
#include "png.h"

namespace p64::decode {
namespace {

constexpr uint32_t kStaticDelayMs = 100;

void read_from_memory(png_structp png, png_bytep out, png_size_t length) {
  auto *self = static_cast<PngDecoder *>(png_get_io_ptr(png));
  if (!self || !self->data_ || self->offset_ + length > self->size_) {
    png_error(png, "read beyond the end of the file");
    return;
  }
  std::memcpy(out, self->data_ + self->offset_, length);
  self->offset_ += length;
}

// Silence libpng's default stderr chatter; errors longjmp back to the caller.
void on_warning(png_structp, png_const_charp) {}

// Non-premultiplied src-over for PNG_BLEND_OP_OVER (APNG spec formula).
inline void blend_over_rgba(uint8_t *dst, const uint8_t *src) {
  const uint32_t a_s = src[3];
  if (a_s == 255u) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 255u;
    return;
  }
  if (a_s == 0u) return;
  const uint32_t a_d = dst[3];
  const uint32_t inv = 255u - a_s;
  const uint32_t denom = 255u * a_s + a_d * inv;  // == 255 * a_out
  if (denom == 0u) {
    dst[0] = dst[1] = dst[2] = dst[3] = 0u;
    return;
  }
  for (int c = 0; c < 3; ++c) {
    const uint32_t num = 255u * a_s * src[c] + a_d * inv * dst[c];
    dst[c] = static_cast<uint8_t>(num / denom);
  }
  dst[3] = static_cast<uint8_t>((denom + 127u) / 255u);
}

}  // namespace

PngDecoder::~PngDecoder() { close(); }

void PngDecoder::fail(const char *why) {
  error_ = why;
  open_ = false;
}

void PngDecoder::close() {
  destroy_stream();
  open_ = false;
  animated_ = false;
  canvas_rgb_.clear();
  canvas_rgb_.shrink_to_fit();
  static_pixels_.clear();
  static_pixels_.shrink_to_fit();
  canvas_rgba_.clear();
  canvas_rgba_.shrink_to_fit();
  subframe_.clear();
  subframe_.shrink_to_fit();
  prev_snapshot_.clear();
  prev_snapshot_.shrink_to_fit();
}

void PngDecoder::destroy_stream() {
  if (png_) {
    png_destroy_read_struct(&png_, pinfo_ ? &pinfo_ : nullptr, nullptr);
    png_ = nullptr;
    pinfo_ = nullptr;
  }
}

bool PngDecoder::open(const uint8_t *data, size_t size, gfx::Rgb background) {
  close();
  background_ = background;
  data_ = data;
  size_ = size;
  offset_ = 0;
  error_ = "";
  info_ = Info{};
  if (!data || size < 8 || png_sig_cmp(data, 0, 8) != 0) {
    error_ = "not a PNG";
    return false;
  }
  // Probe: read the header to learn the size and whether it is an APNG.
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, on_warning);
  if (!png) {
    error_ = "libpng out of memory";
    return false;
  }
  png_infop pinfo = png_create_info_struct(png);
  if (!pinfo) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    error_ = "libpng out of memory";
    return false;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "corrupt PNG header";
    return false;
  }
  png_set_read_fn(png, this, read_from_memory);
  png_read_info(png, pinfo);
  const png_uint_32 width = png_get_image_width(png, pinfo);
  const png_uint_32 height = png_get_image_height(png, pinfo);
  const png_byte color_type = png_get_color_type(png, pinfo);
  if (width == 0 || height == 0 || width > 16384 || height > 16384) {
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "bad PNG dimensions";
    return false;
  }
  info_.width = static_cast<int>(width);
  info_.height = static_cast<int>(height);
  info_.has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) != 0 || png_get_valid(png, pinfo, PNG_INFO_tRNS);
  const bool apng = png_get_valid(png, pinfo, PNG_INFO_acTL) && png_get_num_frames(png, pinfo) > 1;
  png_destroy_read_struct(&png, &pinfo, nullptr);

  canvas_rgb_.assign(static_cast<size_t>(width) * height * 3, 0);
  if (apng) {
    info_.format = Format::Apng;
    info_.animated = true;
    info_.has_alpha = true;  // dispose and blend can introduce transparency
    animated_ = true;
    const size_t rgba_bytes = static_cast<size_t>(width) * height * 4;
    canvas_rgba_.assign(rgba_bytes, 0);
    subframe_.assign(rgba_bytes, 0);
    if (!open_stream()) return false;
    info_.frame_count = num_frames_;
  } else {
    info_.format = Format::Png;
    info_.animated = false;
    info_.frame_count = 1;
    if (!decode_static()) return false;
  }
  frames_emitted_ = 0;
  at_end_ = false;
  open_ = true;
  return true;
}

// Static PNG: decode the whole image at once into static_pixels_ (RGB or RGBA).
bool PngDecoder::decode_static() {
  offset_ = 0;
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, on_warning);
  if (!png) {
    error_ = "libpng out of memory";
    return false;
  }
  png_infop pinfo = png_create_info_struct(png);
  if (!pinfo) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    error_ = "libpng out of memory";
    return false;
  }
  // No objects with destructors live between here and the reads (longjmp).
  png_bytep *rows = nullptr;
  if (setjmp(png_jmpbuf(png))) {
    delete[] rows;
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "corrupt PNG data";
    return false;
  }
  png_set_read_fn(png, this, read_from_memory);
  png_read_info(png, pinfo);
  const png_byte color_type = png_get_color_type(png, pinfo);
  const png_byte bit_depth = png_get_bit_depth(png, pinfo);
  if (bit_depth == 16) png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
  static_has_alpha_ = info_.has_alpha;
  if (static_has_alpha_) {
    if (png_get_valid(png, pinfo, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
  }
  int passes = png_set_interlace_handling(png);
  if (passes <= 0) passes = 1;
  png_read_update_info(png, pinfo);
  const size_t rowbytes = png_get_rowbytes(png, pinfo);
  const size_t expected = static_cast<size_t>(info_.width) * (static_has_alpha_ ? 4 : 3);
  if (rowbytes != expected) {
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "unexpected PNG row format";
    return false;
  }
  static_pixels_.assign(rowbytes * info_.height, 0);
  rows = new png_bytep[info_.height];
  for (int y = 0; y < info_.height; ++y) rows[y] = static_pixels_.data() + static_cast<size_t>(y) * rowbytes;
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < info_.height; ++y) png_read_row(png, rows[y], nullptr);
  }
  png_read_end(png, nullptr);
  delete[] rows;
  png_destroy_read_struct(&png, &pinfo, nullptr);
  return true;
}

// APNG: fresh read structs with the RGBA transforms, header consumed.
bool PngDecoder::open_stream() {
  destroy_stream();
  offset_ = 0;
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, on_warning);
  if (!png) {
    error_ = "libpng out of memory";
    return false;
  }
  png_infop pinfo = png_create_info_struct(png);
  if (!pinfo) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    error_ = "libpng out of memory";
    return false;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "corrupt APNG header";
    return false;
  }
  png_set_read_fn(png, this, read_from_memory);
  png_read_info(png, pinfo);
  if (!png_get_valid(png, pinfo, PNG_INFO_acTL)) {
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "APNG lost its acTL";
    return false;
  }
  const png_byte color_type = png_get_color_type(png, pinfo);
  const png_byte bit_depth = png_get_bit_depth(png, pinfo);
  // Force RGBA8888 rows: dispose and blend need an alpha channel even for alpha-less
  // colour types (DISPOSE_OP_BACKGROUND clears regions to transparent black).
  if (bit_depth == 16) png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
  if (png_get_valid(png, pinfo, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
  int passes = png_set_interlace_handling(png);
  if (passes <= 0) passes = 1;
  png_read_update_info(png, pinfo);
  if (png_get_rowbytes(png, pinfo) != static_cast<size_t>(info_.width) * 4u) {
    png_destroy_read_struct(&png, &pinfo, nullptr);
    error_ = "unexpected APNG row format";
    return false;
  }
  png_ = png;
  pinfo_ = pinfo;
  interlace_passes_ = passes;
  num_frames_ = png_get_num_frames(png, pinfo);
  first_frame_hidden_ = png_get_first_frame_is_hidden(png, pinfo) != 0;
  // The patch counts the hidden default image in num_frames; only animation frames
  // are emitted.
  if (first_frame_hidden_ && num_frames_ > 1) --num_frames_;
  return true;
}

bool PngDecoder::apng_rewind() {
  std::memset(canvas_rgba_.data(), 0, canvas_rgba_.size());
  frames_emitted_ = 0;
  have_pending_dispose_ = false;
  at_end_ = false;
  return open_stream();
}

void PngDecoder::rewind() {
  if (!open_) return;
  if (animated_) {
    if (!apng_rewind()) fail(error_);
  }
  frames_emitted_ = 0;
  at_end_ = false;
}

void PngDecoder::apply_pending_dispose() {
  if (!have_pending_dispose_) return;
  const size_t canvas_stride = static_cast<size_t>(info_.width) * 4u;
  const size_t rect_stride = static_cast<size_t>(pend_w_) * 4u;
  if (pending_dispose_ == PNG_DISPOSE_OP_BACKGROUND) {
    for (uint32_t row = 0; row < pend_h_; ++row) {
      std::memset(canvas_rgba_.data() + (pend_y_ + row) * canvas_stride + pend_x_ * 4u, 0, rect_stride);
    }
  } else if (pending_dispose_ == PNG_DISPOSE_OP_PREVIOUS && !prev_snapshot_.empty()) {
    for (uint32_t row = 0; row < pend_h_; ++row) {
      std::memcpy(canvas_rgba_.data() + (pend_y_ + row) * canvas_stride + pend_x_ * 4u,
                  prev_snapshot_.data() + row * rect_stride, rect_stride);
    }
  }
  have_pending_dispose_ = false;
}

// Decodes the next animation frame and composites it onto the RGBA canvas.
bool PngDecoder::apng_decode_one() {
  if (!png_ || !pinfo_) {
    fail("APNG stream not open");
    return false;
  }
  // Re-arm the error handler in this stack frame.
  if (setjmp(png_jmpbuf(png_))) {
    fail("corrupt APNG frame");
    return false;
  }
  const size_t canvas_stride = static_cast<size_t>(info_.width) * 4u;
  while (true) {
    png_read_frame_head(png_, pinfo_);
    if (!png_get_valid(png_, pinfo_, PNG_INFO_fcTL)) {
      // The hidden default image: decode at canvas size and discard.
      if (frames_emitted_ != 0 || !first_frame_hidden_) png_error(png_, "APNG image without fcTL");
      for (int pass = 0; pass < interlace_passes_; ++pass) {
        for (int row = 0; row < info_.height; ++row) {
          png_read_row(png_, subframe_.data() + static_cast<size_t>(row) * canvas_stride, nullptr);
        }
      }
      continue;
    }
    png_uint_32 w0 = 0, h0 = 0, x0 = 0, y0 = 0;
    png_uint_16 delay_num = 0, delay_den = 0;
    png_byte dispose_op = PNG_DISPOSE_OP_NONE, blend_op = PNG_BLEND_OP_SOURCE;
    png_get_next_frame_fcTL(png_, pinfo_, &w0, &h0, &x0, &y0, &delay_num, &delay_den, &dispose_op, &blend_op);
    if (w0 == 0 || h0 == 0 || x0 + w0 > static_cast<png_uint_32>(info_.width) ||
        y0 + h0 > static_cast<png_uint_32>(info_.height)) {
      png_error(png_, "APNG frame rect out of bounds");
    }
    if (frames_emitted_ == 0 && dispose_op == PNG_DISPOSE_OP_PREVIOUS) dispose_op = PNG_DISPOSE_OP_BACKGROUND;

    apply_pending_dispose();
    const size_t rect_stride = static_cast<size_t>(w0) * 4u;
    if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
      if (prev_snapshot_.empty()) prev_snapshot_.assign(canvas_rgba_.size(), 0);
      for (uint32_t row = 0; row < h0; ++row) {
        std::memcpy(prev_snapshot_.data() + row * rect_stride,
                    canvas_rgba_.data() + (y0 + row) * canvas_stride + x0 * 4u, rect_stride);
      }
    }
    for (int pass = 0; pass < interlace_passes_; ++pass) {
      for (uint32_t row = 0; row < h0; ++row) {
        png_read_row(png_, subframe_.data() + row * rect_stride, nullptr);
      }
    }
    if (blend_op == PNG_BLEND_OP_SOURCE) {
      for (uint32_t row = 0; row < h0; ++row) {
        std::memcpy(canvas_rgba_.data() + (y0 + row) * canvas_stride + x0 * 4u, subframe_.data() + row * rect_stride,
                    rect_stride);
      }
    } else {
      for (uint32_t row = 0; row < h0; ++row) {
        uint8_t *dst = canvas_rgba_.data() + (y0 + row) * canvas_stride + x0 * 4u;
        const uint8_t *src = subframe_.data() + row * rect_stride;
        for (uint32_t col = 0; col < w0; ++col) blend_over_rgba(dst + col * 4u, src + col * 4u);
      }
    }
    pending_dispose_ = dispose_op;
    have_pending_dispose_ = true;
    pend_x_ = x0;
    pend_y_ = y0;
    pend_w_ = w0;
    pend_h_ = h0;
    const uint32_t den = delay_den == 0 ? 100u : delay_den;
    last_delay_ms_ = (static_cast<uint32_t>(delay_num) * 1000u) / den;
    ++frames_emitted_;
    at_end_ = frames_emitted_ >= num_frames_;
    return true;
  }
}

bool PngDecoder::next(uint32_t &delay_ms) {
  if (!open_) return false;
  const size_t pixels = static_cast<size_t>(info_.width) * info_.height;
  if (!animated_) {
    if (static_has_alpha_) {
      flatten_rgba(static_pixels_.data(), canvas_rgb_.data(), pixels, background_);
    } else {
      std::memcpy(canvas_rgb_.data(), static_pixels_.data(), pixels * 3);
    }
    delay_ms = kStaticDelayMs;
    at_end_ = true;
    return true;
  }
  if (at_end_) {
    if (!apng_rewind()) {
      fail(error_);
      return false;
    }
  }
  if (!apng_decode_one()) return false;
  flatten_rgba(canvas_rgba_.data(), canvas_rgb_.data(), pixels, background_);
  delay_ms = browser_delay_ms(Format::Apng, last_delay_ms_);
  return true;
}

}  // namespace p64::decode

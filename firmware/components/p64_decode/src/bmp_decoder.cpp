#include "p64/decode/bmp_decoder.hpp"

#include <cstring>

#include "p64/decode/alpha.hpp"

namespace p64::decode {
namespace {

constexpr uint32_t kStaticDelayMs = 100;
constexpr uint32_t kBiRgb = 0, kBiRle8 = 1, kBiRle4 = 2, kBiBitfields = 3, kBiAlphaBitfields = 6;
constexpr uint32_t kMaxDim = 16384;

inline uint16_t rd_u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t rd_u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t rd_s32(const uint8_t *p) { return static_cast<int32_t>(rd_u32(p)); }

struct Chan {
  uint32_t shift = 0, bits = 0, max = 0;
};

struct Parsed {
  uint32_t width = 0, height = 0, bitcount = 0, compression = 0, pal_count = 0;
  bool top_down = false, has_alpha = false;
  Chan r, g, b, a;
  uint8_t pal[256][3];
  const uint8_t *pixels = nullptr;
  size_t pixels_size = 0;
};

bool chan_from_mask(uint32_t mask, uint32_t bitcount, Chan &ch) {
  if (mask == 0) return false;
  if (bitcount < 32 && (mask >> bitcount) != 0) return false;
  const uint32_t shift = static_cast<uint32_t>(__builtin_ctz(mask));
  const uint32_t run = mask >> shift;
  if ((run & (run + 1u)) != 0) return false;
  uint32_t bits = 0;
  for (uint32_t r = run; r != 0; r >>= 1) ++bits;
  ch.shift = shift;
  ch.bits = bits;
  ch.max = run;
  return true;
}

inline uint8_t chan_extract(uint32_t pix, const Chan &ch) {
  const uint32_t v = (pix >> ch.shift) & ch.max;
  if (ch.bits == 8) return static_cast<uint8_t>(v);
  if (ch.bits > 8) return static_cast<uint8_t>(v >> (ch.bits - 8));
  return static_cast<uint8_t>((v * 255u + (ch.max >> 1)) / ch.max);
}

inline void pal_put(const Parsed &bp, uint8_t *d, uint32_t idx) {
  if (idx >= bp.pal_count) idx = bp.pal_count - 1u;
  d[0] = bp.pal[idx][0];
  d[1] = bp.pal[idx][1];
  d[2] = bp.pal[idx][2];
}

const char *parse_header(const uint8_t *data, size_t size, Parsed &bp) {
  if (size < 14u + 12u) return "file too small";
  const uint32_t off_bits = rd_u32(data + 10);
  const uint32_t hdr_size = rd_u32(data + 14);
  uint32_t width = 0, height = 0, bitcount = 0, clr_used = 0, compression = kBiRgb;
  uint16_t planes = 0;
  size_t pal_entry_size = 4;
  if (hdr_size == 12) {
    width = rd_u16(data + 18);
    height = rd_u16(data + 20);
    planes = rd_u16(data + 22);
    bitcount = rd_u16(data + 24);
    pal_entry_size = 3;
    if (bitcount != 1 && bitcount != 4 && bitcount != 8 && bitcount != 24) return "unsupported core-header depth";
  } else if (hdr_size == 40 || hdr_size == 52 || hdr_size == 56 || hdr_size == 64 || hdr_size == 108 ||
             hdr_size == 124) {
    if (static_cast<uint64_t>(14u) + hdr_size > size) return "truncated header";
    const int32_t w_raw = rd_s32(data + 18), h_raw = rd_s32(data + 22);
    planes = rd_u16(data + 26);
    bitcount = rd_u16(data + 28);
    compression = rd_u32(data + 30);
    clr_used = rd_u32(data + 46);
    if (w_raw <= 0 || h_raw == 0) return "bad dimensions";
    width = static_cast<uint32_t>(w_raw);
    if (h_raw < 0) {
      bp.top_down = true;
      height = static_cast<uint32_t>(-static_cast<int64_t>(h_raw));
    } else {
      height = static_cast<uint32_t>(h_raw);
    }
  } else {
    return "unsupported header";
  }
  if (planes != 1) return "unsupported plane count";
  if (width < 1 || width > kMaxDim || height < 1 || height > kMaxDim) return "dimensions out of range";
  if (bitcount != 1 && bitcount != 4 && bitcount != 8 && bitcount != 16 && bitcount != 24 && bitcount != 32) {
    return "unsupported bit depth";
  }
  switch (compression) {
    case kBiRgb:
      break;
    case kBiRle8:
      if (bitcount != 8 || bp.top_down) return "invalid RLE8 file";
      break;
    case kBiRle4:
      if (bitcount != 4 || bp.top_down) return "invalid RLE4 file";
      break;
    case kBiBitfields:
    case kBiAlphaBitfields:
      if ((bitcount != 16 && bitcount != 32) || hdr_size == 64 || (compression == kBiAlphaBitfields && hdr_size != 40)) {
        return "unsupported BITFIELDS layout";
      }
      break;
    default:
      return "unsupported compression";
  }
  bp.width = width;
  bp.height = height;
  bp.bitcount = bitcount;
  bp.compression = compression;

  if (bitcount == 16 || bitcount == 32) {
    uint32_t r_mask, g_mask, b_mask, a_mask = 0;
    if (bitcount == 16) {
      r_mask = 0x7C00u;
      g_mask = 0x03E0u;
      b_mask = 0x001Fu;
    } else {
      r_mask = 0x00FF0000u;
      g_mask = 0x0000FF00u;
      b_mask = 0x000000FFu;
    }
    if (compression == kBiBitfields || compression == kBiAlphaBitfields) {
      if (hdr_size == 40) {
        const uint64_t mask_len = compression == kBiAlphaBitfields ? 16u : 12u;
        if (14u + 40u + mask_len > size) return "truncated masks";
        r_mask = rd_u32(data + 54);
        g_mask = rd_u32(data + 58);
        b_mask = rd_u32(data + 62);
        if (compression == kBiAlphaBitfields) a_mask = rd_u32(data + 66);
      } else {
        r_mask = rd_u32(data + 14 + 40);
        g_mask = rd_u32(data + 14 + 44);
        b_mask = rd_u32(data + 14 + 48);
        if (hdr_size >= 56) a_mask = rd_u32(data + 14 + 52);
      }
    }
    if (!chan_from_mask(r_mask, bitcount, bp.r) || !chan_from_mask(g_mask, bitcount, bp.g) ||
        !chan_from_mask(b_mask, bitcount, bp.b)) {
      return "invalid colour masks";
    }
    if (a_mask != 0) {
      if (!chan_from_mask(a_mask, bitcount, bp.a)) return "invalid alpha mask";
      bp.has_alpha = true;
    }
  }

  uint64_t pixel_floor = 14u + hdr_size;
  if (compression == kBiBitfields && hdr_size == 40) {
    pixel_floor += 12u;
  } else if (compression == kBiAlphaBitfields) {
    pixel_floor += 16u;
  }
  if (bitcount <= 8) {
    const uint32_t pal_count = clr_used != 0 ? clr_used : (1u << bitcount);
    if (pal_count > 256u) return "palette too large";
    const uint64_t pal_off = 14u + hdr_size;
    const uint64_t pal_end = pal_off + static_cast<uint64_t>(pal_count) * pal_entry_size;
    if (pal_end > size || pal_end > off_bits) return "palette overruns the file";
    for (uint32_t i = 0; i < pal_count; ++i) {
      const uint8_t *e = data + pal_off + static_cast<size_t>(i) * pal_entry_size;
      bp.pal[i][0] = e[2];
      bp.pal[i][1] = e[1];
      bp.pal[i][2] = e[0];
    }
    bp.pal_count = pal_count;
    pixel_floor = pal_end;
  }
  if (off_bits < pixel_floor || off_bits >= size) return "pixel offset out of bounds";
  bp.pixels = data + off_bits;
  bp.pixels_size = size - off_bits;
  if (compression != kBiRle8 && compression != kBiRle4) {
    const uint64_t stride = ((static_cast<uint64_t>(width) * bitcount + 31u) / 32u) * 4u;
    if (stride * height > bp.pixels_size) return "truncated pixel data";
  }
  return nullptr;
}

void decode_uncompressed(const Parsed &bp, uint8_t *out) {
  const uint32_t w = bp.width, h = bp.height;
  const size_t stride = static_cast<size_t>(((static_cast<uint64_t>(w) * bp.bitcount + 31u) / 32u) * 4u);
  const size_t out_bpp = bp.has_alpha ? 4u : 3u;
  for (uint32_t fr = 0; fr < h; ++fr) {
    const uint8_t *src = bp.pixels + static_cast<size_t>(fr) * stride;
    const uint32_t dr = bp.top_down ? fr : (h - 1u - fr);
    uint8_t *dst = out + static_cast<size_t>(dr) * w * out_bpp;
    switch (bp.bitcount) {
      case 1:
        for (uint32_t x = 0; x < w; ++x) pal_put(bp, dst + x * 3u, (src[x >> 3] >> (7u - (x & 7u))) & 1u);
        break;
      case 4:
        for (uint32_t x = 0; x < w; ++x) pal_put(bp, dst + x * 3u, (x & 1u) ? (src[x >> 1] & 0x0Fu) : (src[x >> 1] >> 4));
        break;
      case 8:
        for (uint32_t x = 0; x < w; ++x) pal_put(bp, dst + x * 3u, src[x]);
        break;
      case 16:
        for (uint32_t x = 0; x < w; ++x) {
          const uint32_t pix = rd_u16(src + x * 2u);
          uint8_t *d = dst + x * out_bpp;
          d[0] = chan_extract(pix, bp.r);
          d[1] = chan_extract(pix, bp.g);
          d[2] = chan_extract(pix, bp.b);
          if (bp.has_alpha) d[3] = chan_extract(pix, bp.a);
        }
        break;
      case 24:
        for (uint32_t x = 0; x < w; ++x) {
          const uint8_t *s = src + x * 3u;
          uint8_t *d = dst + x * 3u;
          d[0] = s[2];
          d[1] = s[1];
          d[2] = s[0];
        }
        break;
      default:  // 32
        for (uint32_t x = 0; x < w; ++x) {
          const uint32_t pix = rd_u32(src + x * 4u);
          uint8_t *d = dst + x * out_bpp;
          d[0] = chan_extract(pix, bp.r);
          d[1] = chan_extract(pix, bp.g);
          d[2] = chan_extract(pix, bp.b);
          if (bp.has_alpha) d[3] = chan_extract(pix, bp.a);
        }
        break;
    }
  }
}

inline void rle_put(const Parsed &bp, uint8_t *out, uint32_t x, uint32_t fr, uint32_t idx) {
  const uint32_t dr = bp.height - 1u - fr;
  pal_put(bp, out + (static_cast<size_t>(dr) * bp.width + x) * 3u, idx);
}

const char *decode_rle(const Parsed &bp, uint8_t *out) {
  const bool rle4 = bp.compression == kBiRle4;
  const uint8_t *p = bp.pixels;
  const uint8_t *end = bp.pixels + bp.pixels_size;
  const uint32_t w = bp.width, h = bp.height;
  uint32_t x = 0, fr = 0;
  while (static_cast<size_t>(end - p) >= 2u) {
    const uint8_t n = *p++;
    const uint8_t v = *p++;
    if (n > 0) {
      for (uint32_t i = 0; i < n; ++i) {
        const uint32_t idx = rle4 ? ((i & 1u) ? (v & 0x0Fu) : static_cast<uint32_t>(v >> 4)) : v;
        if (x < w && fr < h) rle_put(bp, out, x, fr, idx);
        ++x;
      }
    } else if (v == 0) {
      x = 0;
      ++fr;
    } else if (v == 1) {
      return nullptr;
    } else if (v == 2) {
      if (static_cast<size_t>(end - p) < 2u) return "RLE delta past the end";
      x += *p++;
      fr += *p++;
    } else {
      const uint32_t bytes = rle4 ? ((static_cast<uint32_t>(v) + 1u) / 2u) : v;
      const uint32_t padded = (bytes + 1u) & ~1u;
      if (static_cast<size_t>(end - p) < padded) return "RLE run past the end";
      for (uint32_t i = 0; i < v; ++i) {
        const uint32_t idx = rle4 ? ((i & 1u) ? (p[i >> 1] & 0x0Fu) : static_cast<uint32_t>(p[i >> 1] >> 4)) : p[i];
        if (x < w && fr < h) rle_put(bp, out, x, fr, idx);
        ++x;
      }
      p += padded;
    }
  }
  return p == end ? nullptr : "RLE stream ends mid-opcode";
}

}  // namespace

bool BmpDecoder::open(const uint8_t *data, size_t size, gfx::Rgb background) {
  open_ = false;
  background_ = background;
  error_ = "";
  info_ = Info{};
  pixels_.clear();
  canvas_rgb_.clear();
  if (!data || size < 2 || data[0] != 'B' || data[1] != 'M') {
    error_ = "not a BMP";
    return false;
  }
  auto bp = std::make_unique<Parsed>();  // ~800 bytes of palette: off the stack
  if (const char *why = parse_header(data, size, *bp)) {
    error_ = why;
    return false;
  }
  info_.format = Format::Bmp;
  info_.width = static_cast<int>(bp->width);
  info_.height = static_cast<int>(bp->height);
  info_.animated = false;
  info_.frame_count = 1;
  info_.has_alpha = bp->has_alpha;
  has_alpha_ = bp->has_alpha;
  const size_t pixels = static_cast<size_t>(bp->width) * bp->height;
  pixels_.assign(pixels * (has_alpha_ ? 4 : 3), 0);
  if (bp->compression == kBiRle8 || bp->compression == kBiRle4) {
    if (const char *why = decode_rle(*bp, pixels_.data())) {
      error_ = why;
      pixels_.clear();
      return false;
    }
  } else {
    decode_uncompressed(*bp, pixels_.data());
  }
  canvas_rgb_.assign(pixels * 3, 0);
  at_end_ = false;
  open_ = true;
  return true;
}

bool BmpDecoder::next(uint32_t &delay_ms) {
  if (!open_) return false;
  const size_t pixels = static_cast<size_t>(info_.width) * info_.height;
  if (has_alpha_) {
    flatten_rgba(pixels_.data(), canvas_rgb_.data(), pixels, background_);
  } else {
    std::memcpy(canvas_rgb_.data(), pixels_.data(), pixels * 3);
  }
  delay_ms = kStaticDelayMs;
  at_end_ = true;
  return true;
}

}  // namespace p64::decode

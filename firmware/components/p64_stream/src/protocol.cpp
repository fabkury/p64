#include "protocol.hpp"

#include <cstring>

namespace p64::stream {
namespace {

uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
uint32_t be32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

}  // namespace

bool parse_raw(const uint8_t *data, size_t len, RawHeader &out, size_t &payload_offset) {
  if (len < kRawHeaderBytes || std::memcmp(data, "P64F", 4) != 0) return false;
  if (data[4] != 1) return false;
  if (data[5] > 2) return false;
  out.format = static_cast<Format>(data[5]);
  out.width = le16(data + 6);
  out.height = le16(data + 8);
  out.sequence = le16(data + 10);
  out.palette = data[12] & 1;
  out.last = data[12] & 2;
  out.offset = le32(data + 14);
  out.total = le32(data + 18);
  if (out.width == 0 || out.height == 0 || out.width > kMaxSide || out.height > kMaxSide) return false;
  const uint32_t bpp = out.format == Format::Rgb888 ? 3 : out.format == Format::Rgb565 ? 2 : 1;
  const uint32_t expected = static_cast<uint32_t>(out.width) * out.height * bpp + (out.palette ? kPaletteBytes : 0);
  if (out.total != expected) return false;
  if (out.format == Format::Indexed8 && !out.palette) return false;
  if (len - kRawHeaderBytes > kMaxChunkPayload) return false;
  if (out.offset > out.total || len - kRawHeaderBytes > out.total - out.offset) return false;
  payload_offset = kRawHeaderBytes;
  return true;
}

bool parse_ddp(const uint8_t *data, size_t len, DdpHeader &out, size_t &payload_offset) {
  if (len < kDdpHeaderBytes) return false;
  const uint8_t flags = data[0];
  if ((flags >> 6) != 1) return false;  // version 1 only
  if (flags & 0x02) return false;       // a query, not data
  out.push = flags & 0x01;
  out.timecode = flags & 0x10;
  out.sequence = data[1] & 0x0F;
  out.type = data[2];
  out.destination = data[3];
  out.offset = be32(data + 4);
  out.length = static_cast<uint16_t>((data[8] << 8) | data[9]);
  payload_offset = kDdpHeaderBytes + (out.timecode ? 4 : 0);
  if (len < payload_offset) return false;
  if (out.length != len - payload_offset) return false;
  return true;
}

Assembler::Assembler(uint8_t *storage, size_t capacity) : buffer_(storage), capacity_(capacity) {
  have_.reserve(capacity / kBlock + 1);
}

void Assembler::begin(uint32_t total) {
  total_ = total;
  received_ = 0;
  active_ = buffer_ && total > 0 && total <= capacity_;
  if (active_) have_.assign(total / kBlock + 1, 0);
}

bool Assembler::add(uint32_t offset, const uint8_t *data, size_t len) {
  if (!active_ || offset > total_ || len > total_ - offset) return false;
  if (len) std::memcpy(buffer_ + offset, data, len);
  // Count each 64-byte block once, so a resent chunk does not inflate the tally. A chunk
  // that covers part of a block marks the whole block: chunks are contiguous per sender,
  // so a partial block is finished by the neighbouring chunk or is the stream's tail.
  const size_t first = offset / kBlock, end = (offset + len + kBlock - 1) / kBlock;
  for (size_t b = first; b < end && b < have_.size(); ++b) {
    if (have_[b]) continue;
    have_[b] = 1;
    const size_t block_start = b * kBlock;
    const size_t block_end = block_start + kBlock < total_ ? block_start + kBlock : total_;
    received_ += static_cast<uint32_t>(block_end - block_start);
  }
  return true;
}

void Assembler::reset() {
  active_ = false;
  total_ = received_ = 0;
}

bool to_rgb888(Format format, int width, int height, bool palette, const uint8_t *data, size_t len, uint8_t *out,
               size_t out_len) {
  if (width <= 0 || height <= 0) return false;
  const size_t pixels = static_cast<size_t>(width) * height;
  if (out_len < pixels * 3) return false;
  const uint8_t *pal = nullptr;
  const uint8_t *px = data;
  size_t avail = len;
  if (palette) {
    if (len < kPaletteBytes) return false;
    pal = data;
    px = data + kPaletteBytes;
    avail -= kPaletteBytes;
  }
  switch (format) {
    case Format::Rgb888:
      if (avail < pixels * 3) return false;
      std::memcpy(out, px, pixels * 3);
      return true;
    case Format::Rgb565:
      if (avail < pixels * 2) return false;
      for (size_t i = 0; i < pixels; ++i) {
        const uint16_t v = le16(px + i * 2);
        const uint8_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
        out[i * 3] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        out[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        out[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
      }
      return true;
    case Format::Indexed8:
      if (!pal || avail < pixels) return false;
      for (size_t i = 0; i < pixels; ++i) {
        const uint8_t *c = pal + px[i] * 3;
        out[i * 3] = c[0];
        out[i * 3 + 1] = c[1];
        out[i * 3 + 2] = c[2];
      }
      return true;
  }
  return false;
}

int ddp_side_for_bytes(uint32_t total) {
  if (total == 64u * 64 * 3) return 64;
  if (total == 128u * 128 * 3) return 128;
  return 0;
}

}  // namespace p64::stream

// The two stream wire formats (spec 8.1, ADR 0007) and frame assembly, pure and
// host-tested: DDP on UDP 4048 and the raw p64 format on UDP 4064. No ESP-IDF includes.
//
// Raw p64 frame (P64F), little-endian, chunked by byte offset:
//   0   "P64F"        magic
//   4   u8 version    1
//   5   u8 format     0 RGB888, 1 RGB565 (little-endian), 2 indexed 8-bit with a palette
//   6   u16 width     1..128
//   8   u16 height    1..128
//   10  u16 sequence  frame number: a change starts a new frame (and counts loss)
//   12  u8 flags      bit 0 palette present (768 bytes of RGB888 first), bit 1 last chunk
//   13  u8 reserved   0
//   14  u32 offset    byte offset of this chunk in the frame's stream (palette + pixels)
//   18  u32 total     total bytes of the frame's stream
//   22  payload       at most 1400 bytes
// A frame is complete when every byte arrived; chunks may come in any order, so the
// "last chunk" bit is informational (a sender's end-of-frame mark), not a cut-off.
//
// DDP (http://www.3waylabs.com/ddp/): 10-byte header, big-endian offset and length,
// flags byte 0x40 = version 1, 0x01 = push (last chunk of the frame), 0x10 = a 4-byte
// timecode follows the header. RGB888 pixels; the frame size follows from the byte count
// at the push: 12288 = 64x64, 49152 = 128x128.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p64::stream {

enum class Format : uint8_t { Rgb888 = 0, Rgb565 = 1, Indexed8 = 2 };

constexpr size_t kMaxSide = 128;
constexpr size_t kMaxPixelBytes = kMaxSide * kMaxSide * 3;
constexpr size_t kPaletteBytes = 256 * 3;
constexpr size_t kMaxFrameBytes = kMaxPixelBytes + kPaletteBytes;  // the assembler's storage
constexpr size_t kRawHeaderBytes = 22;
constexpr size_t kDdpHeaderBytes = 10;
constexpr size_t kMaxChunkPayload = 1400;

struct RawHeader {
  Format format;
  uint16_t width, height, sequence;
  bool palette, last;
  uint32_t offset, total;
};
// Parses a P64F datagram; false when the magic, version, format, sizes or total are wrong.
bool parse_raw(const uint8_t *data, size_t len, RawHeader &out, size_t &payload_offset);

struct DdpHeader {
  bool push;      // last chunk of the frame
  bool timecode;  // four more header bytes follow
  uint8_t sequence;
  uint8_t type;
  uint8_t destination;
  uint32_t offset;
  uint16_t length;
};
// Parses a DDP data datagram; false for other versions, queries or a short packet.
bool parse_ddp(const uint8_t *data, size_t len, DdpHeader &out, size_t &payload_offset);

// Collects the chunks of one frame by byte offset into caller-owned storage (PSRAM on
// the device). `begin()` sizes the stream; `add()` copies a chunk; `complete()` once
// every byte is in (any order; a resent chunk counts once).
class Assembler {
 public:
  Assembler(uint8_t *storage, size_t capacity);
  // Active when 0 < total <= capacity.
  void begin(uint32_t total);
  // False when the chunk falls outside the stream or no frame is open.
  bool add(uint32_t offset, const uint8_t *data, size_t len);
  bool complete() const { return active_ && received_ >= total_; }
  bool active() const { return active_; }
  uint32_t total() const { return total_; }
  uint32_t received() const { return received_; }  // distinct bytes received
  const uint8_t *data() const { return buffer_; }
  void reset();

 private:
  static constexpr size_t kBlock = 64;
  uint8_t *buffer_;
  size_t capacity_;
  std::vector<uint8_t> have_;  // one byte per 64-byte block: received or not
  uint32_t total_ = 0, received_ = 0;
  bool active_ = false;
};

// Turns a frame's stream (palette when indexed, then pixels) into RGB888 of width x
// height in `out` (capacity out_len). False when the stream or `out` is too short.
bool to_rgb888(Format format, int width, int height, bool palette, const uint8_t *data, size_t len, uint8_t *out,
               size_t out_len);

// The frame side a DDP stream implies from its byte count: 64 (12288 bytes) or 128
// (49152 bytes); 0 for any other count.
int ddp_side_for_bytes(uint32_t total);

}  // namespace p64::stream

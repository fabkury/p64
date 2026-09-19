#include <cstring>

#include "p64/decode/decoder.hpp"
#include "p64/decode/gif_decoder.hpp"

namespace p64::decode {

const char *format_name(Format f) {
  switch (f) {
    case Format::Gif:
      return "GIF";
    case Format::Png:
      return "PNG";
    case Format::Apng:
      return "APNG";
    case Format::WebP:
      return "WebP";
    case Format::Bmp:
      return "BMP";
    default:
      return "unknown";
  }
}

namespace {

uint32_t be32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// Walks the PNG chunk list up to the first IDAT looking for acTL (the animation control
// chunk, which the APNG spec requires before IDAT).
bool png_is_animated(const uint8_t *data, size_t size) {
  size_t pos = 8;
  while (pos + 8 <= size) {
    const uint32_t len = be32(data + pos);
    const uint8_t *type = data + pos + 4;
    if (std::memcmp(type, "acTL", 4) == 0) return true;
    if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0) return false;
    const uint64_t next = static_cast<uint64_t>(pos) + 12 + len;
    if (next > size) return false;
    pos = static_cast<size_t>(next);
  }
  return false;
}

}  // namespace

Format sniff(const uint8_t *data, size_t size) {
  if (!data || size < 12) return Format::Unknown;
  if (std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0) return Format::Gif;
  static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  if (std::memcmp(data, png_sig, 8) == 0) return png_is_animated(data, size) ? Format::Apng : Format::Png;
  if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) return Format::WebP;
  if (data[0] == 'B' && data[1] == 'M') return Format::Bmp;
  return Format::Unknown;
}

uint32_t browser_delay_ms(Format format, uint32_t stored_ms) {
  switch (format) {
    case Format::Gif:
      return stored_ms <= 10 ? 100 : stored_ms;
    case Format::Apng:
    case Format::WebP:
      return stored_ms == 0 ? 100 : stored_ms;
    default:
      return stored_ms;
  }
}

std::unique_ptr<Decoder> create(Format format) {
  switch (format) {
    case Format::Gif:
      return std::make_unique<GifDecoder>();
    default:
      return nullptr;
  }
}

}  // namespace p64::decode

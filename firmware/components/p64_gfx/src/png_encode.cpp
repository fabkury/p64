#include "p64/gfx/png_encode.hpp"

#include <cstring>

namespace p64::gfx {
namespace {

uint32_t crc_table[256];
bool crc_ready = false;

void crc_init() {
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crc_table[n] = c;
  }
  crc_ready = true;
}

uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
  if (!crc_ready) crc_init();
  for (size_t i = 0; i < len; ++i) crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}

void put_be32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void put_chunk(std::vector<uint8_t> &out, const char *type, const uint8_t *data, size_t len) {
  put_be32(out, static_cast<uint32_t>(len));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  if (len) out.insert(out.end(), data, data + len);
  const uint32_t crc = crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
  put_be32(out, crc);
}

}  // namespace

std::vector<uint8_t> encode_png_rgb(const uint8_t *rgb, int width, int height) {
  std::vector<uint8_t> out;
  if (!rgb || width <= 0 || height <= 0) return out;
  const size_t row = static_cast<size_t>(width) * 3;
  // Raw scanlines with the filter byte (0 = none) in front of each row.
  std::vector<uint8_t> raw;
  raw.reserve((row + 1) * height);
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + y * row, rgb + (y + 1) * row);
  }
  // zlib stream: header, stored blocks of at most 65535 bytes, adler32.
  std::vector<uint8_t> z;
  z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size() || raw.empty()) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = pos + n >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n));
    z.push_back(static_cast<uint8_t>((~n) >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (raw.empty()) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  put_be32(z, (b << 16) | a);

  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  out.reserve(z.size() + 64);
  out.insert(out.end(), sig, sig + 8);
  uint8_t ihdr[13];
  ihdr[0] = static_cast<uint8_t>(width >> 24);
  ihdr[1] = static_cast<uint8_t>(width >> 16);
  ihdr[2] = static_cast<uint8_t>(width >> 8);
  ihdr[3] = static_cast<uint8_t>(width);
  ihdr[4] = static_cast<uint8_t>(height >> 24);
  ihdr[5] = static_cast<uint8_t>(height >> 16);
  ihdr[6] = static_cast<uint8_t>(height >> 8);
  ihdr[7] = static_cast<uint8_t>(height);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 2;  // colour type RGB
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  put_chunk(out, "IHDR", ihdr, sizeof(ihdr));
  put_chunk(out, "IDAT", z.data(), z.size());
  put_chunk(out, "IEND", nullptr, 0);
  return out;
}

}  // namespace p64::gfx

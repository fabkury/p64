// Host unit tests: the DDP and raw stream wire formats and assembly.
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;



// --- streams: the wire formats and assembly (spec 8.1) ------------------------------

std::vector<uint8_t> raw_packet(uint8_t format, uint16_t w, uint16_t h, uint16_t seq, uint8_t flags, uint32_t offset,
                                uint32_t total, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> p = {'P', '6', '4', 'F', 1, format};
  auto le16 = [&](uint16_t v) { p.push_back(v & 0xFF); p.push_back(v >> 8); };
  auto le32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) p.push_back((v >> (8 * i)) & 0xFF); };
  le16(w); le16(h); le16(seq);
  p.push_back(flags); p.push_back(0);
  le32(offset); le32(total);
  p.insert(p.end(), payload.begin(), payload.end());
  return p;
}

std::vector<uint8_t> ddp_packet(bool push, uint8_t seq, uint32_t offset, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> p = {static_cast<uint8_t>(0x40 | (push ? 1 : 0)), static_cast<uint8_t>(seq & 0x0F), 1, 1};
  for (int i = 3; i >= 0; --i) p.push_back((offset >> (8 * i)) & 0xFF);
  p.push_back((payload.size() >> 8) & 0xFF); p.push_back(payload.size() & 0xFF);
  p.insert(p.end(), payload.begin(), payload.end());
  return p;
}

TEST_CASE("stream_protocol") {
  using namespace p64::stream;
  // Raw header parsing.
  RawHeader h; size_t off = 0;
  std::vector<uint8_t> pay(300, 7);
  auto pk = raw_packet(0, 10, 10, 5, 2, 0, 300, pay);
  CHECK(parse_raw(pk.data(), pk.size(), h, off));
  CHECK(h.format == Format::Rgb888); CHECK_EQ(h.width, 10); CHECK_EQ(h.height, 10); CHECK_EQ(h.sequence, 5);
  CHECK(!h.palette); CHECK(h.last); CHECK_EQ(h.offset, 0u); CHECK_EQ(h.total, 300u); CHECK_EQ(off, kRawHeaderBytes);
  pk = raw_packet(0, 10, 10, 5, 2, 0, 301, pay);           // total does not match the size
  CHECK(!parse_raw(pk.data(), pk.size(), h, off));
  pk = raw_packet(2, 10, 10, 5, 2, 0, 100, pay);           // indexed without a palette
  CHECK(!parse_raw(pk.data(), pk.size(), h, off));
  pk = raw_packet(2, 10, 10, 5, 3, 0, 868, pay);           // indexed with palette: 768 + 100
  CHECK(parse_raw(pk.data(), pk.size(), h, off));
  pk = raw_packet(1, 129, 10, 5, 2, 0, 2580, pay);         // too wide
  CHECK(!parse_raw(pk.data(), pk.size(), h, off));
  pk = raw_packet(0, 10, 10, 5, 2, 200, 300, pay);         // 300 bytes at offset 200 of 300
  CHECK(!parse_raw(pk.data(), pk.size(), h, off));
  pk[4] = 2;                                                // version
  CHECK(!parse_raw(pk.data(), pk.size(), h, off));
  std::vector<uint8_t> big(1401, 0);
  pk = raw_packet(0, 128, 128, 0, 0, 0, 49152, big);       // chunk over the limit
  CHECK(!parse_raw(pk.data(), pk.size(), h, off));

  // DDP header parsing.
  DdpHeader d;
  pay.assign(1440, 1);
  auto dp = ddp_packet(false, 3, 1440, pay);
  CHECK(parse_ddp(dp.data(), dp.size(), d, off));
  CHECK(!d.push); CHECK_EQ(d.sequence, 3); CHECK_EQ(d.offset, 1440u); CHECK_EQ(d.length, 1440); CHECK_EQ(off, 10u);
  dp = ddp_packet(true, 4, 10848, pay);
  CHECK(parse_ddp(dp.data(), dp.size(), d, off));
  CHECK(d.push);
  dp[0] = 0x80;                                             // version 2
  CHECK(!parse_ddp(dp.data(), dp.size(), d, off));
  dp[0] = 0x42;                                             // query
  CHECK(!parse_ddp(dp.data(), dp.size(), d, off));
  dp[0] = 0x50;                                             // timecode: 4 more header bytes, length now wrong
  CHECK(!parse_ddp(dp.data(), dp.size(), d, off));
  dp.insert(dp.begin() + 10, 4, 0);
  CHECK(parse_ddp(dp.data(), dp.size(), d, off));
  CHECK_EQ(off, 14u);
  CHECK_EQ(ddp_side_for_bytes(12288), 64);
  CHECK_EQ(ddp_side_for_bytes(49152), 128);
  CHECK_EQ(ddp_side_for_bytes(12287), 0);

  // Assembly by offset: chunks in any order, duplicates, holes, "last".
  std::vector<uint8_t> storage(kMaxFrameBytes);
  Assembler a(storage.data(), storage.size());
  CHECK(!a.active());
  a.begin(1000);
  CHECK(a.active()); CHECK(!a.complete());
  std::vector<uint8_t> c1(400, 1), c2(400, 2), c3(200, 3);
  CHECK(a.add(400, c2.data(), c2.size()));
  CHECK(!a.complete());
  CHECK(a.add(400, c2.data(), c2.size()));                   // resent: not counted twice
  CHECK_EQ(a.received(), 448u);                              // 400..800 covers blocks 6..12 (64-byte blocks)
  CHECK(a.add(0, c1.data(), c1.size()));
  CHECK(!a.complete());
  CHECK(!a.add(900, c3.data(), c3.size()));                  // past the end
  CHECK(a.add(800, c3.data(), c3.size()));
  CHECK(a.complete());
  CHECK_EQ(a.received(), 1000u);
  CHECK_EQ(a.data()[0], 1); CHECK_EQ(a.data()[500], 2); CHECK_EQ(a.data()[999], 3);
  a.begin(1000);
  CHECK(a.add(800, c3.data(), c3.size()));                   // the tail first: still open
  CHECK(!a.complete());
  CHECK(a.add(0, c1.data(), c1.size()));
  CHECK(!a.complete());
  CHECK(a.add(400, c2.data(), c2.size()));
  CHECK(a.complete());
  a.reset();
  CHECK(!a.active());
  a.begin(kMaxFrameBytes + 1);
  CHECK(!a.active());

  // Conversion to RGB888.
  std::vector<uint8_t> out(4 * 3);
  const uint8_t rgb[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  CHECK(to_rgb888(Format::Rgb888, 2, 2, false, rgb, 12, out.data(), out.size()));
  CHECK_EQ(out[0], 1); CHECK_EQ(out[11], 12);
  CHECK(!to_rgb888(Format::Rgb888, 2, 2, false, rgb, 11, out.data(), out.size()));
  CHECK(!to_rgb888(Format::Rgb888, 2, 2, false, rgb, 12, out.data(), 11));
  const uint8_t rgb565[4] = {0x00, 0xF8, 0xE0, 0x07};       // red, green (little-endian)
  CHECK(to_rgb888(Format::Rgb565, 2, 1, false, rgb565, 4, out.data(), out.size()));
  CHECK_EQ(out[0], 255); CHECK_EQ(out[1], 0); CHECK_EQ(out[2], 0);
  CHECK_EQ(out[3], 0); CHECK_EQ(out[4], 255); CHECK_EQ(out[5], 0);
  const uint8_t blue565[2] = {0x1F, 0x00};
  CHECK(to_rgb888(Format::Rgb565, 1, 1, false, blue565, 2, out.data(), out.size()));
  CHECK_EQ(out[0], 0); CHECK_EQ(out[1], 0); CHECK_EQ(out[2], 255);
  const uint8_t mid565[2] = {0x10, 0x84};                     // 0x8410: r 16, g 32, b 16 -> 132, 130, 132 (bit replication)
  CHECK(to_rgb888(Format::Rgb565, 1, 1, false, mid565, 2, out.data(), out.size()));
  CHECK_EQ(out[0], 132); CHECK_EQ(out[1], 130); CHECK_EQ(out[2], 132);
  std::vector<uint8_t> indexed(kPaletteBytes + 3, 0);
  indexed[3 * 9] = 40; indexed[3 * 9 + 1] = 50; indexed[3 * 9 + 2] = 60;   // palette entry 9
  indexed[3 * 255] = 1; indexed[3 * 255 + 1] = 2; indexed[3 * 255 + 2] = 3;
  indexed[kPaletteBytes] = 9; indexed[kPaletteBytes + 1] = 255; indexed[kPaletteBytes + 2] = 0;
  CHECK(to_rgb888(Format::Indexed8, 3, 1, true, indexed.data(), indexed.size(), out.data(), out.size()));
  CHECK_EQ(out[0], 40); CHECK_EQ(out[1], 50); CHECK_EQ(out[2], 60);
  CHECK_EQ(out[3], 1); CHECK_EQ(out[4], 2); CHECK_EQ(out[5], 3);
  CHECK_EQ(out[6], 0);
  CHECK(!to_rgb888(Format::Indexed8, 3, 1, true, indexed.data(), kPaletteBytes + 2, out.data(), out.size()));
  CHECK(!to_rgb888(Format::Indexed8, 3, 1, false, indexed.data(), indexed.size(), out.data(), out.size()));
}

}  // namespace

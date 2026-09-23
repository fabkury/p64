// Host unit tests: the delay rule, sniffing, the scaler, rotation and gains, blending, the 5x7 text and the bundled fonts.
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


TEST_CASE("delay_rule") {
  using p64::decode::browser_delay_ms;
  using p64::decode::Format;
  CHECK_EQ(browser_delay_ms(Format::Gif, 0), 100u);
  CHECK_EQ(browser_delay_ms(Format::Gif, 10), 100u);
  CHECK_EQ(browser_delay_ms(Format::Gif, 11), 11u);
  CHECK_EQ(browser_delay_ms(Format::Gif, 40), 40u);
  CHECK_EQ(browser_delay_ms(Format::Apng, 0), 100u);
  CHECK_EQ(browser_delay_ms(Format::Apng, 5), 5u);
  CHECK_EQ(browser_delay_ms(Format::WebP, 0), 100u);
  CHECK_EQ(browser_delay_ms(Format::WebP, 16), 16u);
  CHECK_EQ(browser_delay_ms(Format::Bmp, 0), 0u);
}


TEST_CASE("sniff") {
  using p64::decode::Format;
  using p64::decode::sniff;
  const uint8_t gif[16] = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
  CHECK(sniff(gif, sizeof(gif)) == Format::Gif);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  auto chunk = [](std::vector<uint8_t> &v, const char *type, uint32_t len) {
    v.push_back(static_cast<uint8_t>(len >> 24));
    v.push_back(static_cast<uint8_t>(len >> 16));
    v.push_back(static_cast<uint8_t>(len >> 8));
    v.push_back(static_cast<uint8_t>(len));
    v.insert(v.end(), type, type + 4);
    v.insert(v.end(), len + 4, 0);  // data + crc
  };
  chunk(png, "IHDR", 13);
  std::vector<uint8_t> apng = png;
  chunk(png, "IDAT", 4);
  CHECK(sniff(png.data(), png.size()) == Format::Png);
  chunk(apng, "acTL", 8);
  chunk(apng, "IDAT", 4);
  CHECK(sniff(apng.data(), apng.size()) == Format::Apng);
  const uint8_t webp[16] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', ' '};
  CHECK(sniff(webp, sizeof(webp)) == Format::WebP);
  const uint8_t bmp[16] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(sniff(bmp, sizeof(bmp)) == Format::Bmp);
  const uint8_t junk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  CHECK(sniff(junk, sizeof(junk)) == Format::Unknown);
  CHECK(sniff(gif, 4) == Format::Unknown);
}


TEST_CASE("scaler_placement") {
  p64::gfx::Scaler s;
  s.configure(16, 16, 64, 64);
  CHECK_EQ(s.factor(), 4);
  CHECK_EQ(s.out_w(), 64);
  CHECK_EQ(s.out_x(), 0);
  s.configure(32, 32, 64, 64);
  CHECK_EQ(s.factor(), 2);
  s.configure(48, 48, 64, 64);
  CHECK_EQ(s.factor(), 1);
  CHECK_EQ(s.out_x(), 8);
  CHECK_EQ(s.out_y(), 8);
  s.configure(64, 64, 64, 64);
  CHECK_EQ(s.factor(), 1);
  CHECK_EQ(s.out_w(), 64);
  s.configure(128, 128, 64, 64);
  CHECK_EQ(s.factor(), 1);
  CHECK_EQ(s.out_w(), 64);
  CHECK(!s.enlarging());
  s.configure(64, 32, 64, 64);
  CHECK_EQ(s.out_w(), 64);
  CHECK_EQ(s.out_h(), 32);
  CHECK_EQ(s.out_y(), 16);
  s.configure(100, 50, 64, 64);
  CHECK_EQ(s.out_w(), 64);
  CHECK_EQ(s.out_h(), 32);
  s.configure(20, 10, 64, 64);
  CHECK_EQ(s.factor(), 3);
  CHECK_EQ(s.out_w(), 60);
  CHECK_EQ(s.out_h(), 30);
  CHECK_EQ(s.out_x(), 2);
  CHECK_EQ(s.out_y(), 17);
  s.configure(256, 128, 64, 64);
  CHECK_EQ(s.out_w(), 64);
  CHECK_EQ(s.out_h(), 32);
}


TEST_CASE("scaler_pixels") {
  const uint8_t src[2 * 2 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  p64::gfx::Scaler s;
  s.configure(2, 2, 64, 64);
  Frame f;
  s.scale(src, f, Rgb{9, 9, 9});
  CHECK(f.get(0, 0) == (Rgb{255, 0, 0}));
  CHECK(f.get(31, 31) == (Rgb{255, 0, 0}));
  CHECK(f.get(32, 0) == (Rgb{0, 255, 0}));
  CHECK(f.get(0, 32) == (Rgb{0, 0, 255}));
  CHECK(f.get(63, 63) == (Rgb{255, 255, 255}));
  std::vector<uint8_t> big(128 * 128 * 3);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      const uint8_t v = (x % 2) ? 200 : 0;
      big[(y * 128 + x) * 3 + 0] = v;
      big[(y * 128 + x) * 3 + 1] = v;
      big[(y * 128 + x) * 3 + 2] = v;
    }
  s.configure(128, 128, 64, 64);
  s.scale(big.data(), f);
  CHECK(f.get(0, 0) == (Rgb{100, 100, 100}));
  CHECK(f.get(63, 63) == (Rgb{100, 100, 100}));
  const uint8_t wide[4 * 2 * 3] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  s.configure(4, 2, 64, 64);
  s.scale(wide, f, Rgb{7, 8, 9});
  CHECK(f.get(0, 0) == (Rgb{7, 8, 9}));
  CHECK(f.get(0, 15) == (Rgb{7, 8, 9}));
  CHECK(f.get(0, 16) == (Rgb{1, 1, 1}));
  CHECK(f.get(63, 47) == (Rgb{1, 1, 1}));
  CHECK(f.get(63, 48) == (Rgb{7, 8, 9}));
}


TEST_CASE("rotation_and_gains") {
  Frame f;
  f.clear();
  f.set(1, 0, Rgb{200, 100, 50});
  p64::gfx::ChannelLut lut;
  lut.set_gains(100, 100, 100);
  std::vector<uint8_t> out(Frame::bytes());
  auto at = [&](int x, int y) { return Rgb{out[(y * 64 + x) * 3], out[(y * 64 + x) * 3 + 1], out[(y * 64 + x) * 3 + 2]}; };
  p64::gfx::rotate_copy(f, out.data(), p64::gfx::Rotation::R0, lut);
  CHECK(at(1, 0) == (Rgb{200, 100, 50}));
  p64::gfx::rotate_copy(f, out.data(), p64::gfx::Rotation::R90, lut);
  CHECK(at(63, 1) == (Rgb{200, 100, 50}));
  CHECK(at(1, 0) == (Rgb{0, 0, 0}));
  p64::gfx::rotate_copy(f, out.data(), p64::gfx::Rotation::R180, lut);
  CHECK(at(62, 63) == (Rgb{200, 100, 50}));
  p64::gfx::rotate_copy(f, out.data(), p64::gfx::Rotation::R270, lut);
  CHECK(at(0, 62) == (Rgb{200, 100, 50}));
  lut.set_gains(50, 100, 80);
  p64::gfx::rotate_copy(f, out.data(), p64::gfx::Rotation::R0, lut);
  CHECK(at(1, 0) == (Rgb{100, 100, 40}));
  CHECK_EQ(lut.r[255], 128);
  CHECK_EQ(lut.g[255], 255);
}


TEST_CASE("frame_blend") {
  Frame f;
  f.clear(Rgb{0, 0, 0});
  f.blend(3, 3, Rgb{255, 255, 255}, 128);
  CHECK(f.get(3, 3) == (Rgb{128, 128, 128}));
  f.blend(3, 3, Rgb{0, 0, 0}, 0);
  CHECK(f.get(3, 3) == (Rgb{128, 128, 128}));
  f.blend(3, 3, Rgb{10, 20, 30}, 255);
  CHECK(f.get(3, 3) == (Rgb{10, 20, 30}));
  f.set(-1, 0, Rgb{1, 1, 1});
  CHECK(f.get(-1, 0) == (Rgb{0, 0, 0}));
}


// --- M6: the 5x7 font and the Makapix index ----------------------------------------

TEST_CASE("text_font") {
  using namespace p64::gfx::text;
  CHECK(has_glyph('A'));
  CHECK(has_glyph('z'));
  CHECK(has_glyph('7'));
  CHECK(has_glyph('.'));
  CHECK(!has_glyph('~'));
  CHECK_EQ(text_width("", 1, 1), 0);
  CHECK_EQ(text_width("AB", 1, 1), 11);
  CHECK_EQ(text_width("AB", 2, 1), 22);
  CHECK_EQ(text_width("ABC", 1, 0), 15);
  Frame f;
  f.clear(Rgb{0, 0, 0});
  const int w = draw_text(f, 1, 1, "I", Rgb{255, 255, 255}, 1, 1);
  CHECK_EQ(w, 5);
  // 'I': top row all but the corners, middle column, bottom row.
  CHECK(f.get(1, 1) == (Rgb{0, 0, 0}));
  CHECK(f.get(2, 1) == (Rgb{255, 255, 255}));
  CHECK(f.get(3, 4) == (Rgb{255, 255, 255}));
  CHECK(f.get(1, 4) == (Rgb{0, 0, 0}));
  CHECK(f.get(3, 7) == (Rgb{255, 255, 255}));
  CHECK(f.get(3, 8) == (Rgb{0, 0, 0}));
  // Scale 2 doubles every pixel.
  f.clear(Rgb{0, 0, 0});
  draw_char(f, 0, 0, '1', Rgb{9, 9, 9}, 2);
  CHECK(f.get(4, 0) == (Rgb{9, 9, 9}));
  CHECK(f.get(5, 1) == (Rgb{9, 9, 9}));
  CHECK(f.get(0, 0) == (Rgb{0, 0, 0}));
  // Centred text lands in the middle.
  f.clear(Rgb{0, 0, 0});
  draw_centred(f, 20, "0", Rgb{1, 2, 3}, 1, 1);
  CHECK(f.get(29, 20) == (Rgb{0, 0, 0}));
  CHECK(f.get(30, 20) == (Rgb{1, 2, 3}));
}


// --- M7: the bundled fonts, the clock text, the weather model -------------------------

TEST_CASE("fonts") {
  using namespace p64::gfx::fonts;
  CHECK_EQ(kFontCount, 5u);
  const Font *ch = by_name("capital-hill");
  const Font *ev = by_name("everyday-typical");
  CHECK((ch != nullptr && ev != nullptr));
  CHECK(by_name("nope") == nullptr);
  CHECK(by_name("everyday") == nullptr);  // renamed to everyday-typical on 2026-09-23
  CHECK(&default_font() == ch);
  CHECK_EQ(ch->size, 6);
  CHECK_EQ(ev->size, 7);
  // Every font: a label, unique names, and "12:34" in cap height (digits never descend).
  for (size_t i = 0; i < kFontCount; ++i) {
    const Font &f = *kFonts[i];
    CAPTURE(f.name);
    CHECK(f.label[0] != 0);
    CHECK(by_name(f.name) == &f);
    Frame g;
    g.clear(Rgb{0, 0, 0});
    draw(g, f, 1, 1, "12:34", Rgb{255, 255, 255}, 1);
    int lit_below = 0;
    for (int y = 1 + cap_height(f, 1); y < 64; ++y)
      for (int x = 0; x < 64; ++x) lit_below += g.get(x, y).r ? 1 : 0;
    CHECK_EQ(lit_below, 0);
    // The overlay's fonts keep HH:MM small enough for a corner.
    if (f.overlay) CHECK(width(f, "23:59", 1) <= 32);
  }
  CHECK_EQ(by_name("everyday-slight")->size, 5);
  CHECK_EQ(by_name("everyday-standard")->size, 6);
  CHECK(!by_name("high-birth")->overlay);
  CHECK(width(*ch, "", 1) == 0);
  CHECK((width(*ch, "12:34", 1) > 20 && width(*ch, "12:34", 1) < 40));
  CHECK_EQ(width(*ch, "12:34", 2), 2 * width(*ch, "12:34", 1));
  CHECK_EQ(cap_height(*ch, 2), 12);
  CHECK(line_height(*ch, 1) >= 6);
  // Drawing "1" puts ink in the cap-height box and nothing above it.
  Frame f;
  f.clear(Rgb{0, 0, 0});
  draw(f, *ch, 4, 10, "1", Rgb{255, 255, 255}, 1);
  int inked = 0, above = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      if (f.get(x, y).r == 0) continue;
      ++inked;
      if (y < 10 || y >= 10 + 6) ++above;
    }
  }
  CHECK(inked >= 6);
  CHECK_EQ(above, 0);
  // An outline surrounds the glyph with the outline colour.
  f.clear(Rgb{0, 0, 0});
  const Rgb black{9, 9, 9};
  draw(f, *ch, 10, 10, "1", Rgb{255, 255, 255}, 1, &black);
  bool halo = false;
  for (int y = 8; y < 20; ++y) {
    for (int x = 8; x < 20; ++x) {
      if (f.get(x, y) == black) halo = true;
    }
  }
  CHECK(halo);
  // Everyday has descenders below the cap height.
  CHECK(line_height(*ev, 1) > cap_height(*ev, 1));
}

}  // namespace

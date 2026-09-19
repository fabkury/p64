// p64 -- host test program (built by tests/host/run.py with the PC's gcc/g++).
//
//   p64_hosttest unit                              unit tests, exit 1 on failure
//   p64_hosttest dump <w> <h> <out_dir> <file>...   dump every frame of each file's first loop
//
// dump writes <out_dir>/<basename>.frames:
//   "P64FRM\n<format> <w> <h> <ox> <oy> <ow> <oh> <animated> <has_alpha>\n<n>\n<delay_0> ... \n"
//   then per frame: w*h*3 bytes of canvas RGB888, then dst_w*dst_h*3 bytes scaled.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "p64/decode/decoder.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/gfx/scaler.hpp"
#include "p64/playback/frame_queue.hpp"
#include "cJSON.h"
#include "p64/content/history.hpp"
#include "p64/content/playset.hpp"
#include "p64/content/playset_json.hpp"
#include "p64/content/scheduler.hpp"
#include "p64/content/makapix_index.hpp"
#include "p64/gfx/text.hpp"
#include "p64/gfx/fonts.hpp"
#include "clock_format.hpp"
#include "weather_model.hpp"
#include "weather_icons.hpp"
#include "protocol.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                       \
  do {                                                                                    \
    ++g_checks;                                                                           \
    if (!(cond)) {                                                                        \
      ++g_failures;                                                                       \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                                     \
  } while (0)

#define CHECK_EQ(a, b)                                                                                     \
  do {                                                                                                     \
    ++g_checks;                                                                                            \
    const auto va_ = (a);                                                                                  \
    const auto vb_ = (b);                                                                                  \
    if (!(va_ == vb_)) {                                                                                   \
      ++g_failures;                                                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b,           \
                   static_cast<long long>(va_), static_cast<long long>(vb_));                              \
    }                                                                                                      \
  } while (0)

using p64::gfx::Frame;
using p64::gfx::Rgb;

void test_delay_rule() {
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

void test_sniff() {
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

void test_scaler_placement() {
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

void test_scaler_pixels() {
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

void test_rotation_and_gains() {
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

void test_frame_blend() {
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

void test_frame_queue() {
  static p64::playback::ReadySlot slots[p64::playback::FrameQueue::kSlots];
  p64::playback::FrameQueue q(slots);
  CHECK(q.consumer_peek() == nullptr);
  for (unsigned i = 0; i < p64::playback::FrameQueue::kSlots; ++i) {
    auto *s = q.producer_slot();
    CHECK(s != nullptr);
    s->generation = 1;
    s->due_us = i;
    q.producer_publish();
  }
  CHECK(q.producer_slot() == nullptr);
  CHECK_EQ(q.published(), p64::playback::FrameQueue::kSlots);
  auto *c = q.consumer_peek();
  CHECK(c != nullptr && c->due_us == 0);
  q.consumer_release();
  CHECK(q.producer_slot() != nullptr);
  auto *s = q.producer_slot();
  s->generation = 2;
  q.producer_publish();
  CHECK_EQ(q.latest_generation(), 2u);
  c = q.consumer_peek();
  CHECK(c != nullptr && c->due_us == 1 && c->generation == 1);
}

// --- p64_content (M5): playset model, JSON, scheduler, history ---------------------

void test_playset_model() {
  using namespace p64::content;
  CHECK(valid_playset_name("mix_1"));
  CHECK(!valid_playset_name(""));
  CHECK(!valid_playset_name("has space"));
  CHECK(!valid_playset_name(std::string(33, 'a')));
  Builtin b;
  CHECK(builtin_from_name("local", b) && b == Builtin::Local);
  CHECK(builtin_from_name("PROMOTED", b) && b == Builtin::Promoted);
  CHECK(!builtin_from_name("mine", b));
  Playset p;
  p.name = "Local";
  p.channels.push_back(ChannelSpec{ChannelKind::Local, "", "", 0, 0});
  std::string e;
  CHECK(!p.validate(e));  // reserved name
  p.name = "mine";
  CHECK(p.validate(e));
  p.channels.push_back(ChannelSpec{ChannelKind::MakapixArtist, "bad sqid!", "", 1, 0});
  CHECK(!p.validate(e));
  p.channels.back().identifier = "Ab12";
  CHECK(p.validate(e));
  p.channels.push_back(ChannelSpec{ChannelKind::Pinned, "x", "", 0, 0});
  CHECK(!p.validate(e));
  p.channels.pop_back();
  CHECK(p.channels[0].default_display_name() == "animations");
  CHECK(p.channels[1].default_display_name() == "Artist Ab12");
  CHECK(!p.equal_weights());
  Playset loc = builtin_playset(Builtin::Local, {"", "pixel", "photos"});
  CHECK_EQ(loc.channels.size(), 3u);
  CHECK(loc.builtin);
  CHECK(loc.equal_weights());
  CHECK(loc.validate(e));
  CHECK(builtin_playset(Builtin::Followed, {}).channels.empty());
  CHECK(builtin_playset(Builtin::Promoted, {}).channels[0].kind == ChannelKind::MakapixPromoted);
}

void test_playset_json() {
  using namespace p64::content;
  const char *text =
      "{\"name\":\"mix\",\"channels\":[{\"kind\":\"local\",\"identifier\":\"pixel\",\"weight\":2},"
      "{\"type\":\"named\",\"name\":\"promoted\",\"weight\":1},"
      "{\"type\":\"user\",\"identifier\":\"Q1\",\"display_name\":\"Q\",\"offset\":3}]}";
  cJSON *j = cJSON_Parse(text);
  CHECK(j != nullptr);
  Playset p;
  std::string e;
  CHECK(playset_from_json(j, p, e));
  cJSON_Delete(j);
  CHECK(p.name == "mix");
  CHECK_EQ(p.channels.size(), 3u);
  CHECK(p.channels[0].kind == ChannelKind::Local && p.channels[0].identifier == "pixel" && p.channels[0].weight == 2);
  CHECK(p.channels[1].kind == ChannelKind::MakapixPromoted);
  CHECK(p.channels[2].kind == ChannelKind::MakapixArtist && p.channels[2].offset == 3 && p.channels[2].display_name == "Q");
  CHECK(p.validate(e));
  cJSON *out = playset_to_json(p);
  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  std::string round(s);
  cJSON_free(s);
  CHECK(round.find("\"kind\":\"promoted\"") != std::string::npos);
  CHECK(round.find("\"display_name\":\"Promoted\"") != std::string::npos);
  cJSON *j2 = cJSON_Parse(round.c_str());
  Playset p2;
  CHECK(playset_from_json(j2, p2, e));
  cJSON_Delete(j2);
  CHECK_EQ(p2.channels.size(), 3u);
  CHECK_EQ(p2.channels[2].offset, 3u);
  cJSON *bad = cJSON_Parse("{\"channels\":[{\"kind\":\"giphy\"}]}");
  CHECK(!playset_from_json(bad, p, e));
  cJSON_Delete(bad);
  bad = cJSON_Parse("{\"channels\":5}");
  CHECK(!playset_from_json(bad, p, e));
  cJSON_Delete(bad);
}

void test_scheduler_weights() {
  using namespace p64::content;
  Scheduler s;
  s.configure({0, 0, 0}, {0, 0, 0}, 42);
  CHECK_EQ(s.select_channel(), -1);  // nothing available
  s.set_count(0, 10);
  s.set_count(1, 10);
  s.set_count(2, 0);
  CHECK_EQ(s.channel(0).weight, 32768u);
  CHECK_EQ(s.channel(1).weight, 32768u);
  CHECK_EQ(s.channel(2).weight, 0u);
  s.configure({3, 1, 0}, {0, 0, 0}, 42);
  s.set_count(0, 5);
  s.set_count(1, 5);
  s.set_count(2, 5);
  CHECK_EQ(s.channel(0).weight, 49152u);
  CHECK_EQ(s.channel(1).weight, 16384u);
  CHECK_EQ(s.channel(2).weight, 0u);  // muted
  // SWRR: exactly 3:1 over every 4 picks, the muted channel never.
  s.set_channel_select(ChannelSelect::Swrr);
  int counts[3] = {0, 0, 0};
  for (int i = 0; i < 400; ++i) {
    const int c = s.select_channel();
    CHECK(c >= 0 && c < 3);
    if (c >= 0) ++counts[c];
  }
  CHECK_EQ(counts[0], 300);
  CHECK_EQ(counts[1], 100);
  CHECK_EQ(counts[2], 0);
  bool consecutive = false;
  int last = -1;
  for (int i = 0; i < 100; ++i) {
    const int c = s.select_channel();
    if (c == 1 && last == 1) consecutive = true;
    last = c;
  }
  CHECK(!consecutive);
  // Stochastic: the shares hold within 5 % over 4000 picks.
  s.configure({3, 1, 0}, {0, 0, 0}, 7);
  s.set_count(0, 5);
  s.set_count(1, 5);
  s.set_count(2, 5);
  s.set_channel_select(ChannelSelect::Stochastic);
  int sc[3] = {0, 0, 0};
  for (int i = 0; i < 4000; ++i) {
    const int c = s.select_channel();
    if (c >= 0) ++sc[c];
  }
  CHECK(sc[0] > 2850 && sc[0] < 3150);
  CHECK(sc[1] > 850 && sc[1] < 1150);
  CHECK_EQ(sc[2], 0);
  // Equal weights among the channels with entries; an emptied channel gets no picks.
  s.configure({0, 0}, {0, 0}, 1);
  s.set_count(0, 3);
  s.set_count(1, 3);
  s.set_channel_select(ChannelSelect::Swrr);
  int eq[2] = {0, 0};
  for (int i = 0; i < 10; ++i) ++eq[s.select_channel()];
  CHECK_EQ(eq[0], 5);
  CHECK_EQ(eq[1], 5);
  s.set_count(1, 0);
  for (int i = 0; i < 5; ++i) CHECK_EQ(s.select_channel(), 0);
  CHECK_EQ(s.available_channels(), 1u);
}

void test_scheduler_picks() {
  using namespace p64::content;
  Scheduler s;
  s.configure({0}, {2}, 99);
  CHECK_EQ(s.pick_entry(0, -1), -1);
  s.set_count(0, 5);
  s.set_pick_mode(PickMode::Recency);
  CHECK_EQ(s.pick_entry(0, -1), 2);  // the cursor starts at the offset
  CHECK_EQ(s.pick_entry(0, -1), 3);
  CHECK_EQ(s.pick_entry(0, -1), 4);
  CHECK_EQ(s.pick_entry(0, -1), 0);  // and wraps
  CHECK_EQ(s.pick_entry(0, -1), 1);
  CHECK_EQ(s.pick_entry(0, -1), 2);
  CHECK_EQ(s.pick_entry(0, 3), 4);  // the current artwork is skipped
  s.reset_cursor(0);
  CHECK_EQ(s.pick_entry(0, -1), 2);
  s.configure({0}, {7}, 99);
  s.set_count(0, 5);
  s.set_pick_mode(PickMode::Recency);
  CHECK_EQ(s.pick_entry(0, -1), 2);  // an offset beyond the count wraps
  s.set_count(0, 2);
  const int shrunk = s.pick_entry(0, -1);
  CHECK(shrunk >= 0 && shrunk < 2);
  // Random: in range, no immediate repeat when avoidable, every entry visited.
  s.configure({0}, {0}, 5);
  s.set_count(0, 8);
  s.set_pick_mode(PickMode::Random);
  bool seen[8] = {false, false, false, false, false, false, false, false};
  int last = -1;
  bool repeat = false;
  for (int i = 0; i < 400; ++i) {
    const int p = s.pick_entry(0, last);
    CHECK(p >= 0 && p < 8);
    if (p == last) repeat = true;
    seen[p] = true;
    last = p;
  }
  CHECK(!repeat);
  for (bool b : seen) CHECK(b);
  s.set_count(0, 1);
  CHECK_EQ(s.pick_entry(0, 0), 0);  // a single entry repeats, there is nothing else
  Scheduler a, b;
  a.configure({1, 1}, {0, 0}, 1234);
  b.configure({1, 1}, {0, 0}, 1234);
  a.set_count(0, 9);
  a.set_count(1, 9);
  b.set_count(0, 9);
  b.set_count(1, 9);
  for (int i = 0; i < 50; ++i) {
    const int ca = a.select_channel(), cb = b.select_channel();
    CHECK_EQ(ca, cb);
    CHECK_EQ(a.pick_entry(ca, -1), b.pick_entry(cb, -1));
  }
}

void test_history() {
  using namespace p64::content;
  History h;
  CHECK(h.current() == nullptr);
  CHECK(!h.back());
  CHECK(!h.forward());
  auto item = [](const std::string &name) {
    HistoryItem i;
    i.name = name;
    return i;
  };
  h.push(item("a"));
  h.push(item("b"));
  h.push(item("c"));
  CHECK_EQ(h.size(), 3u);
  CHECK_EQ(h.position(), 2u);
  CHECK(h.current()->name == "c");
  CHECK(h.back());
  CHECK(h.current()->name == "b");
  CHECK(h.back());
  CHECK(h.current()->name == "a");
  CHECK(!h.back());
  CHECK(h.forward());
  CHECK(h.current()->name == "b");
  h.push(item("d"));  // from the middle: what was ahead is discarded
  CHECK_EQ(h.size(), 3u);
  CHECK(h.current()->name == "d");
  CHECK(!h.forward());
  CHECK(h.back());
  CHECK(h.current()->name == "b");
  CHECK(h.go_to(2));
  CHECK(h.current()->name == "d");
  CHECK(!h.go_to(3));
  h.remove(0);
  CHECK_EQ(h.size(), 2u);
  CHECK(h.current()->name == "d");
  CHECK_EQ(h.position(), 1u);
  h.remove(1);
  CHECK(h.current()->name == "b");
  CHECK_EQ(h.position(), 0u);
  h.clear();
  for (int i = 0; i < 40; ++i) h.push(item(std::to_string(i)));
  CHECK_EQ(h.size(), 32u);
  CHECK(h.at(0).name == "8");
  CHECK(h.current()->name == "39");
  for (int i = 0; i < 31; ++i) CHECK(h.back());
  CHECK(!h.back());
  CHECK(h.current()->name == "8");
}

// --- M6: the 5x7 font and the Makapix index ----------------------------------------

void test_text_font() {
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

void test_makapix_index() {
  using namespace p64::content;
  uint8_t key[16];
  CHECK(parse_uuid("550e8400-e29b-41d4-a716-446655440000", key));
  CHECK_EQ(key[0], 0x55);
  CHECK_EQ(key[15], 0x00);
  CHECK(format_uuid(key) == "550e8400-e29b-41d4-a716-446655440000");
  CHECK(parse_uuid("550E8400E29B41D4A716446655440000", key));
  CHECK(!parse_uuid("550e8400-e29b-41d4-a716-44665544000", key));
  CHECK(!parse_uuid("zz0e8400-e29b-41d4-a716-446655440000", key));
  std::string shard, file;
  MakapixFormat fmt;
  CHECK(split_art_url("https://vault.makapix.club/21/32/abc-def.png", shard, file, fmt));
  CHECK(shard == "21/32");
  CHECK(file == "abc-def.png");
  CHECK(fmt == MakapixFormat::Png);
  CHECK(split_art_url("http://vault.makapix.club/a1/b2/c3/k.webp?x=1", shard, file, fmt));
  CHECK(shard == "a1/b2/c3");
  CHECK(file == "k.webp");
  CHECK(fmt == MakapixFormat::WebP);
  CHECK(!split_art_url("nonsense", shard, file, fmt));
  CHECK_EQ(parse_iso8601_utc("1970-01-01T00:00:10Z"), 10u);
  CHECK_EQ(parse_iso8601_utc("2024-01-15T09:00:00Z"), 1705309200u);
  CHECK_EQ(parse_iso8601_utc("2024-01-15T09:00:00.123456+00:00"), 1705309200u);
  CHECK_EQ(parse_iso8601_utc("2024-01-15T10:00:00+01:00"), 1705309200u);
  CHECK_EQ(parse_iso8601_utc("garbage"), 0u);

  MakapixEntry a = {};
  a.post_id = 1;
  parse_uuid("550e8400-e29b-41d4-a716-446655440000", a.storage_key);
  std::snprintf(a.sqid, sizeof(a.sqid), "k5fNx");
  std::snprintf(a.shard, sizeof(a.shard), "21/32");
  a.format = static_cast<uint8_t>(MakapixFormat::Gif);
  a.flags = kMakapixCached;
  a.width = a.height = 64;
  a.modified_at = 100;
  MakapixEntry b = a;
  b.post_id = 2;
  b.flags = kMakapixMissing;
  b.storage_key[0] = 0xAB;
  CHECK(cache_relative_path(b) == "cache/ab/ab0e8400-e29b-41d4-a716-446655440000.gif");
  MakapixEntries prev;
  prev.push_back(a);
  prev.push_back(b);
  const std::vector<uint8_t> bytes = serialize_index(prev);
  CHECK_EQ(bytes.size(), 16u + 2 * 64u);
  MakapixEntries back;
  std::string e;
  CHECK(deserialize_index(bytes.data(), bytes.size(), back, e));
  CHECK_EQ(back.size(), 2u);
  CHECK_EQ(back[1].post_id, 2);
  CHECK(std::string(back[0].sqid) == "k5fNx");
  std::vector<uint8_t> corrupt = bytes;
  corrupt[20] ^= 1;
  CHECK(!deserialize_index(corrupt.data(), corrupt.size(), back, e));
  CHECK(!deserialize_index(bytes.data(), bytes.size() - 1, back, e));
  CHECK(!deserialize_index(bytes.data(), 3, back, e));
  MakapixEntries empty;
  const std::vector<uint8_t> none = serialize_index(empty);
  CHECK(deserialize_index(none.data(), none.size(), back, e) && back.empty());

  // Merge: a fresh listing without post 1, with post 2 unchanged and a new post 3.
  MakapixEntries fresh;
  MakapixEntry b2 = b;
  b2.flags = 0;
  b2.sqid[0] = 0;  // the RPC listing carries no sqid: keep the old one
  fresh.push_back(b2);
  MakapixEntry c = a;
  c.post_id = 3;
  c.flags = 0;
  fresh.push_back(c);
  CHECK_EQ(merge_index(prev, fresh), 1u);  // post 1 dropped
  CHECK_EQ(fresh.size(), 2u);
  CHECK_EQ(fresh[0].flags, kMakapixMissing);
  CHECK(std::string(fresh[0].sqid) == "k5fNx");
  CHECK_EQ(fresh[1].flags, 0);
  // A changed file (modified_at) loses its flags.
  MakapixEntries fresh2;
  MakapixEntry a2 = a;
  a2.flags = 0;
  a2.modified_at = 200;
  fresh2.push_back(a2);
  CHECK_EQ(merge_index(prev, fresh2), 1u);
  CHECK_EQ(fresh2[0].flags, 0);
}

// --- M7: the bundled fonts, the clock text, the weather model -------------------------

void test_fonts() {
  using namespace p64::gfx::fonts;
  CHECK_EQ(kFontCount, 2u);
  const Font *ch = by_name("capital-hill");
  const Font *ev = by_name("everyday");
  CHECK(ch != nullptr && ev != nullptr);
  CHECK(by_name("nope") == nullptr);
  CHECK(&default_font() == ch);
  CHECK_EQ(ch->size, 6);
  CHECK_EQ(ev->size, 7);
  CHECK(width(*ch, "", 1) == 0);
  CHECK(width(*ch, "12:34", 1) > 20 && width(*ch, "12:34", 1) < 40);
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

void test_clock_format() {
  using namespace p64::widgets::clock_format;
  tm t = {};
  t.tm_hour = 14;
  t.tm_min = 5;
  t.tm_sec = 9;
  t.tm_wday = 6;
  t.tm_mday = 19;
  t.tm_mon = 8;
  CHECK(time_text(t, true, false) == "14:05");
  CHECK(time_text(t, true, true) == "14:05:09");
  CHECK(time_text(t, false, false) == " 2:05");
  CHECK(time_text(t, true, false, false) == "14 05");
  CHECK(meridiem(t, false) == "PM");
  CHECK(meridiem(t, true) == "");
  t.tm_hour = 0;
  CHECK(time_text(t, false, false) == "12:05");
  CHECK(meridiem(t, false) == "AM");
  CHECK(date_text(t, false) == "Sat 19 Sep");
  CHECK(date_text(t, true) == "Sat Sep 19");
}

void test_weather_model() {
  using namespace p64::widgets::weather_model;
  using p64::widgets::icons::Group;
  using p64::widgets::icons::group_for_code;
  CHECK(group_for_code(0) == Group::Clear);
  CHECK(group_for_code(2) == Group::Partly);
  CHECK(group_for_code(3) == Group::Overcast);
  CHECK(group_for_code(45) == Group::Fog);
  CHECK(group_for_code(53) == Group::Drizzle);
  CHECK(group_for_code(63) == Group::Rain);
  CHECK(group_for_code(66) == Group::Freezing);
  CHECK(group_for_code(75) == Group::Snow);
  CHECK(group_for_code(81) == Group::Showers);
  CHECK(group_for_code(86) == Group::SnowShowers);
  CHECK(group_for_code(95) == Group::Thunder);
  CHECK(group_for_code(99) == Group::Hail);
  CHECK_EQ(weekday_of("2026-09-19"), 6);  // a Saturday
  CHECK_EQ(weekday_of("2024-01-15"), 1);  // a Monday
  CHECK_EQ(weekday_of("nope"), -1);
  const std::string url = request_url(48.85f, 2.35f, true);
  CHECK(url.find("latitude=48.8500") != std::string::npos);
  CHECK(url.find("temperature_unit=fahrenheit") != std::string::npos);
  CHECK(request_url(0, 0, false).find("fahrenheit") == std::string::npos);
  const char *reply =
      "{\"current_units\":{\"temperature_2m\":\"\xC2\xB0\x43\"},"
      "\"current\":{\"temperature_2m\":21.4,\"relative_humidity_2m\":55,\"weather_code\":61,\"is_day\":0},"
      "\"daily\":{\"time\":[\"2026-09-19\",\"2026-09-20\",\"2026-09-21\",\"2026-09-22\"],"
      "\"weather_code\":[61,2,0,95],\"temperature_2m_max\":[24,22,20,18],\"temperature_2m_min\":[15,14,12,11]}}";
  Forecast f;
  std::string e;
  CHECK(parse(reply, std::strlen(reply), f, e));
  CHECK(f.valid);
  CHECK(!f.imperial);
  CHECK(f.temperature > 21.3f && f.temperature < 21.5f);
  CHECK_EQ(f.humidity, 55);
  CHECK_EQ(f.code, 61);
  CHECK(!f.is_day);
  CHECK_EQ(f.today.weekday, 6);
  CHECK(f.today.max == 24.0f && f.today.min == 15.0f);
  CHECK_EQ(f.day_count, 3);
  CHECK_EQ(f.days[0].weekday, 0);
  CHECK_EQ(f.days[2].code, 95);
  CHECK(!parse("{\"reason\":\"bad\"}", 16, f, e));
  CHECK(e == "bad");
  CHECK(!parse("garbage", 7, f, e));
}


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

void test_stream_protocol() {
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

int run_unit() {
  test_delay_rule();
  test_sniff();
  test_scaler_placement();
  test_scaler_pixels();
  test_rotation_and_gains();
  test_frame_blend();
  test_frame_queue();
  test_playset_model();
  test_playset_json();
  test_scheduler_weights();
  test_scheduler_picks();
  test_history();
  test_text_font();
  test_makapix_index();
  test_fonts();
  test_clock_format();
  test_weather_model();
  test_stream_protocol();
  std::printf("unit tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

std::string basename_of(const std::string &path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

int run_dump(int argc, char **argv) {
  if (argc < 6) {
    std::fprintf(stderr, "usage: p64_hosttest dump <dst_w> <dst_h> <out_dir> <file>...\n");
    return 2;
  }
  const int dst_w = std::atoi(argv[2]);
  const int dst_h = std::atoi(argv[3]);
  const std::string out_dir = argv[4];
  int failures = 0;
  for (int i = 5; i < argc; ++i) {
    const std::string path = argv[i];
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "%s: cannot read\n", path.c_str());
      ++failures;
      continue;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const p64::decode::Format format = p64::decode::sniff(bytes.data(), bytes.size());
    std::unique_ptr<p64::decode::Decoder> dec = p64::decode::create(format);
    if (!dec) {
      std::fprintf(stderr, "%s: unknown format\n", path.c_str());
      ++failures;
      continue;
    }
    if (!dec->open(bytes.data(), bytes.size(), Rgb{0, 0, 0})) {
      std::fprintf(stderr, "%s: open failed: %s\n", path.c_str(), dec->error());
      ++failures;
      continue;
    }
    const int w = dec->info().width, h = dec->info().height;
    p64::gfx::Scaler scaler;
    scaler.configure(w, h, dst_w, dst_h);
    std::vector<std::vector<uint8_t>> canvases, scaled;
    std::vector<uint32_t> delays;
    uint32_t loops = 0;
    while (true) {
      uint32_t delay = 0;
      const bool was_at_end = dec->at_end();
      if (was_at_end) break;
      if (!dec->next(delay)) {
        std::fprintf(stderr, "%s: decode failed at frame %u: %s\n", path.c_str(),
                     static_cast<unsigned>(canvases.size()), dec->error());
        ++failures;
        break;
      }
      // GIFs with trailing data wrap unannounced: a frame belonging to the second loop
      // is recognised by the decoder's loop counter (GIF only exposes it this way).
      if (format == p64::decode::Format::Gif) {
        // GifDecoder::loops() is not part of the interface; detect the wrap through
        // info().frame_count, which the decoder fills when the loop ends.
        if (dec->info().frame_count != 0 && canvases.size() >= dec->info().frame_count) {
          ++loops;
          break;
        }
      }
      canvases.emplace_back(dec->canvas(), dec->canvas() + static_cast<size_t>(w) * h * 3);
      std::vector<uint8_t> out(static_cast<size_t>(dst_w) * dst_h * 3);
      scaler.scale(dec->canvas(), out.data());
      scaled.push_back(std::move(out));
      delays.push_back(delay);
      if (canvases.size() > 4096) break;
    }
    (void)loops;
    const std::string out_path = out_dir + "/" + basename_of(path) + ".frames";
    std::FILE *out = std::fopen(out_path.c_str(), "wb");
    if (!out) {
      std::fprintf(stderr, "%s: cannot write %s\n", path.c_str(), out_path.c_str());
      ++failures;
      continue;
    }
    std::fprintf(out, "P64FRM\n%s %d %d %d %d %d %d %d %d\n%u\n", p64::decode::format_name(format), w, h, scaler.out_x(),
                 scaler.out_y(), scaler.out_w(), scaler.out_h(), dec->info().animated ? 1 : 0,
                 dec->info().has_alpha ? 1 : 0, static_cast<unsigned>(canvases.size()));
    for (size_t k = 0; k < delays.size(); ++k) std::fprintf(out, "%s%u", k ? " " : "", static_cast<unsigned>(delays[k]));
    std::fprintf(out, "\n");
    for (size_t k = 0; k < canvases.size(); ++k) {
      std::fwrite(canvases[k].data(), 1, canvases[k].size(), out);
      std::fwrite(scaled[k].data(), 1, scaled[k].size(), out);
    }
    std::fclose(out);
  }
  return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc >= 2 && std::strcmp(argv[1], "unit") == 0) return run_unit();
  if (argc >= 2 && std::strcmp(argv[1], "dump") == 0) return run_dump(argc, argv);
  std::fprintf(stderr, "usage: p64_hosttest unit | dump <w> <h> <out_dir> <file>...\n");
  return 2;
}

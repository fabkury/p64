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
  p64::playback::FrameQueue q;
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

int run_unit() {
  test_delay_rule();
  test_sniff();
  test_scaler_placement();
  test_scaler_pixels();
  test_rotation_and_gains();
  test_frame_blend();
  test_frame_queue();
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

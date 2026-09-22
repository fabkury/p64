// p64 -- host test program (built by tests/host/run.py with the PC's gcc/g++).
//
//   p64_hosttest [doctest options]                 unit tests (doctest; the files under unit/)
//   p64_hosttest unit [doctest options]            the same (the first argument is dropped)
//   p64_hosttest dump <w> <h> <out_dir> <file>...  dump every frame of each file's first loop
//
// doctest options of use: -tc="scheduler*" runs matching cases, -s shows every check,
// -r=junit writes JUnit XML for CI, -ltc lists the cases.
//
// dump writes <out_dir>/<basename>.frames:
//   "P64FRM" NL "<format> <w> <h> <ox> <oy> <ow> <oh> <animated> <has_alpha>" NL "<n>" NL "<delay_0> ..." NL
//   then per frame: w*h*3 bytes of canvas RGB888, then dst_w*dst_h*3 bytes scaled.

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "p64/decode/decoder.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/gfx/scaler.hpp"

namespace {

using p64::gfx::Rgb;

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
  if (argc >= 2 && std::strcmp(argv[1], "dump") == 0) return run_dump(argc, argv);
  doctest::Context context;
  if (argc >= 2 && std::strcmp(argv[1], "unit") == 0) {
    context.applyCommandLine(argc - 1, argv + 1);
  } else {
    context.applyCommandLine(argc, argv);
  }
  return context.run();
}

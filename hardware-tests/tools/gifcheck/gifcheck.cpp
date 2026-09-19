// p64 -- PC harness for gifcheck.py: decodes GIF files with the firmware's own player
// (AnimatedGIF + GifPlayer + Scaler) and dumps every frame of the first loop.
//
// Usage: gifcheck <dst_w> <dst_h> <out_dir> <gif>...
// For each GIF it writes <out_dir>/<basename>.frames:
//   header: "P64GIF\n<w> <h> <ox> <oy> <ow> <oh>\n"
//   then per frame: w*h*3 bytes of canvas RGB888, then dst_w*dst_h*3 bytes of scaled RGB888.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gif_player.hpp"

namespace {

std::string basename_of(const std::string &path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: gifcheck <dst_w> <dst_h> <out_dir> <gif>...\n");
    return 2;
  }
  const int dst_w = std::atoi(argv[1]);
  const int dst_h = std::atoi(argv[2]);
  const std::string out_dir = argv[3];
  int failures = 0;

  for (int i = 4; i < argc; ++i) {
    const std::string path = argv[i];
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "%s: cannot read\n", path.c_str());
      ++failures;
      continue;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    p64::GifPlayer player;
    if (!player.open(bytes.data(), bytes.size())) {
      std::fprintf(stderr, "%s: open failed (error %d)\n", path.c_str(), player.last_error());
      ++failures;
      continue;
    }
    p64::Scaler scaler;
    scaler.configure(player.width(), player.height(), dst_w, dst_h);

    const std::string out_path = out_dir + "/" + basename_of(path) + ".frames";
    std::FILE *out = std::fopen(out_path.c_str(), "wb");
    if (!out) {
      std::fprintf(stderr, "%s: cannot write %s\n", path.c_str(), out_path.c_str());
      ++failures;
      continue;
    }
    std::fprintf(out, "P64GIF\n%d %d %d %d %d %d\n", player.width(), player.height(), scaler.out_x(), scaler.out_y(),
                 scaler.out_w(), scaler.out_h());

    std::vector<uint8_t> scaled(static_cast<size_t>(dst_w) * dst_h * 3);
    const size_t canvas_bytes = static_cast<size_t>(player.width()) * player.height() * 3;
    int frames = 0;
    while (true) {
      if (!player.next_frame()) {
        std::fprintf(stderr, "%s: decode error %d at frame %d\n", path.c_str(), player.last_error(), frames);
        ++failures;
        break;
      }
      if (player.loops() > 0) break;  // wrapped around: the first loop is complete
      scaler.scale(player.canvas(), scaled.data());
      std::fwrite(player.canvas(), 1, canvas_bytes, out);
      std::fwrite(scaled.data(), 1, scaled.size(), out);
      ++frames;
      if (frames > 100000) break;  // safety net
    }
    std::fclose(out);
    std::printf("%s: %d frames, %dx%d -> %dx%d at (%d,%d)\n", basename_of(path).c_str(), frames, player.width(),
                player.height(), scaler.out_w(), scaler.out_h(), scaler.out_x(), scaler.out_y());
  }
  return failures ? 1 : 0;
}

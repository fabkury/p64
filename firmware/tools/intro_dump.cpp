// The host side of tools/render_intro.py: renders every frame of every intro animation
// (or one, by name) into a raw RGB stream on stdout, so Python can make the GIF gallery
// without a device. Built by the tool with g++ from p64_gfx's frame.cpp and p64_intro.
//
//   intro_dump <window_ms> <seed> [name]
//
// Output: for each animation, a header line "name frames\n" then frames * 64*64*3 bytes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "p64/gfx/frame.hpp"
#include "p64/intro/intro.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: intro_dump <window_ms> <seed> [name]\n");
    return 2;
  }
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  const unsigned window_ms = static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10));
  std::vector<uint8_t> scratch(p64::intro::kScratchBytes);
  p64::intro::Context ctx;
  ctx.seed = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
  ctx.scratch = scratch.data();
  const char *only = argc > 3 ? argv[3] : nullptr;
  const int frames = static_cast<int>(window_ms / p64::intro::kFrameMs);
  auto frame = std::make_unique<p64::gfx::Frame>();
  for (int i = 0; i < p64::intro::count(); ++i) {
    const char *name = p64::intro::name(i);
    if (only && std::strcmp(only, name) != 0) continue;
    std::printf("%s %d\n", name, frames);
    std::fflush(stdout);
    for (int k = 0; k < frames; ++k) {
      p64::intro::render(i, *frame, ctx, static_cast<float>(k * p64::intro::kFrameMs) / static_cast<float>(window_ms));
      std::fwrite(frame->data(), 1, p64::gfx::Frame::bytes(), stdout);
    }
    std::fflush(stdout);
  }
  return 0;
}

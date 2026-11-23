// p64 -- large buffers that are refused instead of aborting (finding F-21, 2026-11-23).
//
// A whole artwork file is held in one buffer of up to 5 MB: a download, an upload, a file
// read from the card. C++ exceptions are off, so a std::vector that cannot allocate
// aborts and the device reboots; and the buffers that can exist at once (the memory
// window's 6 MB, a download, an upload, the current and the prepared artwork) add up to
// more than the 16 MB of PSRAM. Every such buffer is therefore taken through this file:
// it is granted only while the PSRAM keeps kReserve free for everything else, and a
// refusal is an ordinary failure of that one file ("not enough memory now"), which the
// caller reports and survives.
//
// The rules are pure (host-tested, tests/host/unit/system.cpp); reserve() and resize()
// are the shell (big_alloc.cpp), which reads the heap and allocates under one lock, so
// two buffers asked for at the same moment cannot both be granted the same room.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p64::system::big {

// Buffers below this are ordinary allocations: the reserve covers them.
constexpr size_t kThreshold = 64 * 1024;
// What a granted buffer leaves free in PSRAM: canvases of two artworks (256x256), the
// decoders' working memory, channel indexes, JSON documents, lwIP's buffers.
constexpr size_t kReserve = 2 * 1024 * 1024;
// The text of a refusal, the same wherever it is reported.
inline constexpr const char *kRefused = "not enough memory for this file now";

// Whether a buffer of `want` bytes may be taken with `free_bytes` free in PSRAM, the
// largest free block being `largest_block`. A buffer below the threshold always may.
constexpr bool admits(size_t want, size_t free_bytes, size_t largest_block) {
  if (want < kThreshold) return true;
  return largest_block >= want && free_bytes >= want && free_bytes - want >= kReserve;
}

// The next capacity of a buffer whose final size is unknown (a download without a
// Content-Length): at least `need`, doubling from 16 KB, never above `max_bytes` (the
// caller has already refused a body larger than that, so need <= max_bytes). Doubling
// past the cap is what made a 4.1 MB body ask for 8 MB beside the 4 MB it held.
constexpr size_t grown(size_t capacity, size_t need, size_t max_bytes) {
  size_t next = capacity < 8 * 1024 ? 16 * 1024 : capacity * 2;
  if (next > max_bytes) next = max_bytes;
  return next < need ? need : next;
}

struct Stats {
  uint32_t granted = 0;  // buffers at or above the threshold that were given
  uint32_t refused = 0;  // and those that were not
  size_t last_refused_bytes = 0;
};

// Makes room for `capacity` bytes in `v` (its contents kept). False when the rule
// refuses: `v` is unchanged and the caller fails that one file with kRefused.
bool reserve(std::vector<uint8_t> &v, size_t capacity);
// The same, then sets the size.
bool resize(std::vector<uint8_t> &v, size_t size);
Stats stats();

}  // namespace p64::system::big

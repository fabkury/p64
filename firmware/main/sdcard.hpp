// p64 -- microSD card: SDMMC 1-bit (CLK 1, CMD 44, D0 17 on the Waveshare board), FAT
// file system at /sdcard, GIF files in the card's root. Mounted once at boot (no
// card-detect line, so no hot plug; /sd/mount remounts on request). Reads and writes
// go through the FAT VFS; callers keep them off the rendering task.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p64::sdcard {

struct FileInfo {
  std::string name;
  size_t size = 0;
};

struct Info {
  bool mounted = false;
  std::string card;      // product name from the CID
  uint64_t total = 0;    // bytes
  uint64_t free = 0;     // bytes
  std::string error;     // last mount failure
};

// Mounts the card (formats it when that is enabled and the mount fails). No-op when
// disabled in menuconfig. Returns whether the card is usable.
bool mount();
// Unmounts and mounts again (after the user swapped the card).
bool remount();
bool mounted();
Info info();

// True for a plain file name (no path parts, no control characters, not too long).
bool valid_name(const std::string &name);
// Full path of a file in the card's root.
std::string path_of(const std::string &name);

// GIF files in the root, sorted by name.
std::vector<FileInfo> list_gifs();
// Reads a whole file into `out` (cleared first); `error` explains a failure.
bool read_file(const std::string &name, std::vector<uint8_t> &out, size_t max_bytes, std::string &error);
bool remove_file(const std::string &name, std::string &error);

}  // namespace p64::sdcard

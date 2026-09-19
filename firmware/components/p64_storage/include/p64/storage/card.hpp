// p64 -- the microSD card: SDMMC 1-bit (CLK 1, CMD 44, D0 17 on the Waveshare board),
// FAT at /sdcard, the p64 layout under a root folder (spec section 14). No card-detect
// line: a swapped card is picked up on the next mount attempt. Every call here does
// card I/O and must run on a core-0 task, never on the render or player task.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p64::storage {

struct FileInfo {
  std::string name;  // file name without the directory
  size_t size = 0;
  bool directory = false;
};

struct CardInfo {
  bool mounted = false;
  std::string card;    // product name from the CID
  uint64_t total = 0;  // FAT capacity in bytes
  uint64_t free = 0;   // free bytes
  std::string error;   // last mount failure, empty when mounted
};

// Mount point of the card and the p64 root under it.
const char *mount_point();           // "/sdcard"
std::string root();                  // "/sdcard/p64" (configurable later)
std::string animations_dir();        // root()/animations
std::string downloads_dir();         // root()/downloads
std::string cache_dir();             // root()/cache
std::string channels_dir();          // root()/channels
std::string state_dir();             // root()/state

// Mounts the card. Never formats it (spec: formatting is an explicit user action).
// Creates the p64 folders when the card is writable. Returns whether the card is usable.
bool mount();
// Unmounts and mounts again (after the user swapped the card).
bool remount();
bool mounted();
CardInfo info();

// True for a plain file or folder name (no path parts, no control characters).
bool valid_name(const std::string &name);
// Lower-cased extension of a name, including the dot ("" when none).
std::string extension_of(const std::string &name);

// Entries of a directory (files and folders, hidden entries skipped), sorted by name.
std::vector<FileInfo> list(const std::string &dir);
bool exists(const std::string &path);
// Reads a whole file into `out` (cleared first); refuses files above `max_bytes`.
bool read_file(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes, std::string &error);

}  // namespace p64::storage

// p64 -- the microSD card: SDMMC 1-bit (CLK 1, CMD 44, D0 17 on the Waveshare board),
// FAT at /sdcard, the p64 layout under a root folder (spec section 14). The card is
// mounted once, at boot (ADR 0018): there is no card-detect line and no remount; a card
// that stops answering trips a latch (fail_latch.hpp) and counts as not mounted until
// the restart. Every call here does card I/O and must run on a core-0 task, never on the
// render or player task.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p64/storage/fail_latch.hpp"
#include "sdkconfig.h"

namespace p64::storage {

struct FileInfo {
  std::string name;  // file name without the directory
  size_t size = 0;
  bool directory = false;
  int64_t mtime = 0;  // modification time as stat() reports it (FAT local time), 0 when unknown
};

struct CardInfo {
  bool mounted = false;
  std::string card;    // product name from the CID
  uint64_t total = 0;  // FAT capacity in bytes
  uint64_t free = 0;   // free bytes
  std::string error;   // why no card is in use ("" when mounted): the mount failure, or the failure since
  bool failed = false;       // it was mounted and stopped answering (the latch; until the restart)
  bool unformatted = false;  // a card answers but has no FAT file system: format_unformatted() can take it
};

// Mount point of the card and the p64 root under it.
const char *mount_point();           // "/sdcard"
std::string root();                  // "/sdcard/p64" (configurable later)
std::string animations_dir();        // root()/animations
std::string downloads_dir();         // root()/downloads
std::string cache_dir();             // root()/cache
std::string channels_dir();          // root()/channels
std::string state_dir();             // root()/state

#ifdef CONFIG_P64_TEST_HOOKS
// The test build's virtual eject (system/test_hooks.hpp): from now until the reboot the
// card counts as not mounted for `reason` (one of the mount errors, e.g. "no card
// answers"), and mount() refuses. Call it before the first mount().
void test_eject(const std::string &reason);
// From now on every read and write of a file fails with an I/O error, as a card pulled
// out would make them: the latch then trips by its own rule.
void test_fail_io();
#endif

// Mounts the card. Never formats it (spec: formatting is an explicit user action).
// Creates the p64 folders when the card is writable. Returns whether the card is usable.
bool mount();
// Formats the mounted card (FAT32, spec 14: an explicit user action with confirmation)
// and recreates the p64 folders. Everything on the card is lost.
bool format(std::string &error);
// A card that answered at boot without a FAT file system (info().unformatted): formats
// it (FAT32) and mounts it. The caller restarts the device afterwards (decision D20), so
// that everything starts in card mode. Never formats a card that has a file system.
bool format_unformatted(std::string &error);
// Mounted and answering. False from the moment the latch trips.
bool mounted();
// Called once, from the task that saw the last error, when the latch trips.
void on_failed(void (*callback)());
CardInfo info();

// True for a plain file or folder name (no path parts, no control characters).
bool valid_name(const std::string &name);
// Lower-cased extension of a name, including the dot ("" when none).
std::string extension_of(const std::string &name);

// Entries of a directory (files and folders, hidden entries skipped), sorted by name,
// with size and mtime from the directory entries themselves: one pass over the
// directory, where a stat() per entry is a search of the directory each time (a FAT
// directory has no index), quadratic in a folder of thousands (2026-09-29: 8158 cached
// files cost 230 s that way).
std::vector<FileInfo> list(const std::string &dir);
bool exists(const std::string &path);
// Reads a whole file into `out` (cleared first); refuses files above `max_bytes`.
bool read_file(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes, std::string &error);

// Resolves a path given relative to the card root ("animations/x.gif") into an absolute
// one, refusing anything with ".." or control characters. Empty relative = the root.
bool resolve(const std::string &relative, std::string &absolute, std::string &error);
bool is_directory(const std::string &path);
bool remove_path(const std::string &path, std::string &error);  // files and empty folders
bool make_dir(const std::string &path, std::string &error);
bool rename_path(const std::string &from, const std::string &to, std::string &error);
// Atomic write: the data goes to a temporary name next to `path` and is renamed over it
// when complete (partial uploads never appear as files).
bool write_file(const std::string &path, const uint8_t *data, size_t len, std::string &error);

}  // namespace p64::storage

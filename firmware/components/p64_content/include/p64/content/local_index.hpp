// p64 -- the index of a local channel: the artwork files of one folder of the card's
// animations tree, newest first (spec 5.1, 5.3). POSIX directory calls only, no ESP-IDF;
// runs on a core-0 task (card I/O).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p64/content/psram.hpp"

namespace p64::content {

struct LocalEntry {
  char name[128];    // file name (entries with longer names are skipped and counted)
  uint32_t mtime;    // modification time, seconds since the epoch (0 when unknown)
  uint32_t size;     // bytes
  uint8_t missing;   // 1 once a pick found the file gone; a rescan clears it
  uint8_t rejected;  // 1 once the file failed to decode; never retried until a rescan
};

using LocalEntries = std::vector<LocalEntry, PsramAllocator<LocalEntry>>;

// True for the extensions the decoders handle (the content is sniffed when loaded).
bool artwork_extension(const char *name);

// Scans one folder (no recursion) for artwork files, sorted by mtime descending then
// name, at most `cap` entries. `skipped` counts files left out (long names, cap).
bool scan_folder(const std::string &dir, size_t cap, LocalEntries &out, std::string &error, uint32_t *skipped);

// First-level subfolders of `dir`, sorted by name; hidden ones skipped.
bool list_subfolders(const std::string &dir, std::vector<std::string> &out, std::string &error);

}  // namespace p64::content

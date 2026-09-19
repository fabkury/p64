// p64 -- Loader: the core-0 worker that does the show's card I/O: reading artwork files
// into PSRAM and opening their decoders, and scanning the folders of local channels.
// Results come back through callbacks on the loader task; the show forwards them to
// its own queue, so the main task never blocks on the card (architecture section 2).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "p64/content/local_index.hpp"
#include "p64/content/playset.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/playback/artwork.hpp"

namespace p64::loader {

struct LoadResult {
  uint32_t id = 0;
  std::string path;
  std::shared_ptr<playback::Artwork> artwork;  // null on failure
  std::string error;
  bool missing = false;  // the file is not there (as opposed to unreadable or undecodable)
  uint32_t read_ms = 0;
  uint32_t open_ms = 0;
  size_t bytes = 0;
};

struct ScanResult {
  uint32_t generation = 0;
  content::Playset playset;                    // the Local built-in comes back rebuilt from the folders found
  std::vector<content::LocalEntries> entries;  // one per channel (empty for non-local kinds)
  std::vector<std::string> errors;             // one per channel ("" when the scan went fine)
  uint32_t skipped = 0;                        // files left out (long names, caps)
  uint32_t took_ms = 0;
};

using LoadCallback = std::function<void(LoadResult *)>;  // the callee owns the result
using ScanCallback = std::function<void(ScanResult *)>;

bool start(LoadCallback on_load, ScanCallback on_scan);
// Reads and opens an artwork; returns the request id (never 0) the result will carry.
uint32_t load(const std::string &path, gfx::Rgb background);
// Scans the local channels of a playset (rebuilding the Local built-in's channel list
// from the folders present). The result carries `generation` back.
void scan(uint32_t generation, const content::Playset &playset);

}  // namespace p64::loader

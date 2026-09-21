// Where Makapix things live: channel indexes under channels/ and artwork files under
// cache/ on the card (spec 5.4), or a bounded memory cache in PSRAM when there is no
// card (ADR 0006); play-this downloads under downloads/. Card I/O: worker task only.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "p64/content/makapix_index.hpp"

namespace p64::makapix::cache {

bool has_card();
// The index file of a channel (channels/<id>.p64x); load returns the file's mtime as
// the last refresh time.
bool load_index(const std::string &channel_id, content::MakapixEntries &out, uint32_t &last_refresh);
bool save_index(const std::string &channel_id, const content::MakapixEntries &entries, std::string &error);
bool remove_index(const std::string &channel_id);

// An entry's file: an absolute card path, or "mem:<uuid>.<ext>" without a card.
std::string artwork_path(const content::MakapixEntry &e);
bool artwork_present(const content::MakapixEntry &e);
bool store_artwork(const content::MakapixEntry &e, const std::vector<uint8_t> &bytes, std::string &error);
// A play-this download: downloads/<name> on the card, or "mem:dl-<name>".
std::string store_download(const std::string &name, const std::vector<uint8_t> &bytes, std::string &error);
// Bytes of a "mem:" path.
bool memory_bytes(const std::string &path, std::vector<uint8_t> &out);
// Free space on the card (0 without one).
uint64_t free_bytes();
// Removes cached files of entries that vanished from an index (best effort).
void remove_artwork(const content::MakapixEntry &e);

// Walks cache/ (its shards), downloads/ and channels/ and deletes (or, dry, only counts)
// every file whose mtime is older than `older_than_s` before `now` or implausible
// (before 2026, or more than a day in the future). `on_artwork_deleted` gets the file
// name of each cache/ file that went. Yields between shards.
struct SweepStats {
  uint32_t examined = 0, deleted = 0, indexes_deleted = 0, downloads_deleted = 0;
  uint64_t bytes = 0, freed = 0;
};
void sweep(uint32_t now, uint32_t older_than_s, bool dry_run, SweepStats &stats,
           const std::function<void(const std::string &name)> &on_artwork_deleted);

}  // namespace p64::makapix::cache

// p64 -- the index of a Makapix channel (spec 5.3): what the server listed, newest
// first, up to the per-channel cap, with the flags the device keeps about each entry
// (cached on the card, missing on the server, undecodable). 64 bytes per entry in
// PSRAM; a binary file on the card between runs. Pure data: parsing, merging and the
// file format are host-tested. The network side lives in p64_makapix.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p64/content/psram.hpp"

namespace p64::content {

enum class MakapixFormat : uint8_t { Png = 0, Gif = 1, WebP = 2, Bmp = 3, Unknown = 255 };

enum MakapixFlags : uint8_t {
  kMakapixCached = 1,    // the file is in the cache
  kMakapixMissing = 2,   // the server has no such file (404); not retried until the entry changes
  kMakapixRejected = 4,  // downloaded but undecodable; never retried until the entry changes
};

struct MakapixEntry {
  int32_t post_id;
  uint8_t storage_key[16];  // the UUID as bytes
  char sqid[12];            // public sqid, "" when the listing did not carry it
  char shard[12];           // the vault path fragment ("21/32"), verbatim from art_url
  uint8_t format;           // MakapixFormat of the native file
  uint8_t flags;            // MakapixFlags
  uint16_t width;
  uint16_t height;
  uint16_t frame_count;     // 0 = unknown
  uint32_t created_at;      // epoch seconds
  uint32_t modified_at;     // artwork_modified_at, epoch seconds (0 = unknown)
  uint32_t reserved;
};
static_assert(sizeof(MakapixEntry) == 64, "MakapixEntry is the on-card record; keep it 64 bytes");

using MakapixEntries = std::vector<MakapixEntry, PsramAllocator<MakapixEntry>>;

// The size limit (setting "maximum artwork size"): both sides within `max_side`. An
// entry whose size the listing did not carry (0) passes; the decoder judges it later.
inline bool fits_side(const MakapixEntry &e, uint16_t max_side) { return e.width <= max_side && e.height <= max_side; }

// --- helpers ------------------------------------------------------------------------
bool parse_uuid(const char *text, uint8_t out[16]);  // "8-4-4-4-12" hex, case-insensitive
std::string format_uuid(const uint8_t bytes[16]);    // lower-case
MakapixFormat makapix_format_from_name(const std::string &name);  // "png", "gif", "webp", "bmp"
const char *makapix_format_ext(MakapixFormat f);                 // ".png" ...
// The shard and format from an art_url such as https://vault.makapix.club/21/32/<uuid>.png:
// everything between the host and the file name, verbatim (the server says not to parse it).
bool split_art_url(const std::string &art_url, std::string &shard, std::string &file_name, MakapixFormat &format);
// The file name in the cache: "<uuid>.<ext>"; the cache folder shards by the first two hex digits.
std::string cache_relative_path(const MakapixEntry &e);  // "cache/ab/<uuid>.png"
// ISO 8601 UTC ("2024-01-15T09:00:00Z" or with fractions/offset) to epoch seconds; 0 when unparsable.
uint32_t parse_iso8601_utc(const std::string &s);

// --- the file format -----------------------------------------------------------------
// "P64X" magic, version, count, CRC32 of the entries; then the entries.
std::vector<uint8_t> serialize_index(const MakapixEntries &entries);
bool deserialize_index(const uint8_t *data, size_t len, MakapixEntries &out, std::string &error);

// Merges a fresh listing (newest first, the server's order) with the previous index:
// entries keep their flags when their post_id is known and modified_at is unchanged;
// a changed modified_at clears the flags (re-download); entries the fresh listing lacks
// are dropped. Returns how many entries were dropped.
size_t merge_index(const MakapixEntries &previous, MakapixEntries &fresh);

uint32_t crc32_of(const uint8_t *data, size_t len);

}  // namespace p64::content

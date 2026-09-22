// GitHub's "latest release" document and the release's checksum file, parsed without any
// transport (host-tested in tests/host/unit/ota.cpp; review of 2026-09-22, P-T2).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace p64::ota::release {

struct Release {
  std::string version;  // the tag without a leading 'v'
  std::string notes;    // the body, shortened to notes_max characters plus "..."
  std::string bin_url;  // the asset named `asset`, "" when the release lacks it
  std::string sha_url;  // the asset named `asset` + ".sha256", "" when absent
  uint32_t size = 0;    // the image asset's size
};

// False (with `error`) for unreadable JSON or a release without a tag. A release without
// the image asset parses (bin_url stays empty) so the caller can say which asset is
// missing.
bool parse(const char *json, size_t len, const std::string &asset, size_t notes_max, Release &out,
           std::string &error);

// 64 hex digits into 32 bytes (either case).
bool hex_to_bin(const std::string &hex, uint8_t out[32]);

// The digest of a checksum file ("<64 hex>  p64.bin", as sha256sum writes it; anything
// before the first hex digit is skipped).
bool digest_from_checksum_file(const std::string &text, uint8_t out[32]);

}  // namespace p64::ota::release

// p64 -- device playsets (ADR 0018, decisions D9 to D12): the user playsets of a cardless
// p64, kept on the device itself (NVS) instead of the card. Up to 8, of up to 16
// channels each, none of them local; a list of its own, separate from the card's
// playsets, and nothing moves between the two.
//
// Each playset is one string in a slot of a small key-value store (the firmware gives the
// NVS state store; the host tests a map), the playset's JSON with its name inside. The
// name rules are those of every user playset (ADR 0017): any printable text of 1 to 32
// code points, the same playset up to ASCII case and spacing. A slot that does not parse
// is skipped and logged by the caller, never fatal, and can be overwritten.
//
// Pure: no ESP-IDF include; host-tested (tests/host/unit/device_playsets.cpp).
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "p64/content/playset.hpp"
#include "p64/content/playset_store.hpp"

namespace p64::content::device_playsets {

constexpr size_t kMaxPlaysets = 8;
constexpr size_t kMaxChannels = 16;
// One playset's JSON (an NVS string holds 4000 bytes), and all of them together: NVS is
// 64 KB shared with the settings, the Wi-Fi data and the Makapix credentials.
constexpr size_t kMaxPlaysetBytes = 3800;
constexpr size_t kMaxTotalBytes = 16 * 1024;

// The store underneath: slot keys are "dps0" to "dps7".
struct Backend {
  std::function<bool(const std::string &key, std::string &value)> get;  // false when absent
  std::function<bool(const std::string &key, const std::string &value)> set;
  std::function<bool(const std::string &key)> erase;
};

// The same contract as the card's store (playset_store.hpp), with the device's limits.
bool list(const Backend &b, std::vector<store::Summary> &out, std::string &error);
bool exists(const Backend &b, const std::string &name);
bool load(const Backend &b, const std::string &name, Playset &out, std::string &error);
// Refuses a 9th playset, more than 16 channels, a local channel, a playset too large for
// its slot or for the total. An update under another spelling keeps the stored spelling.
bool save(const Backend &b, Playset &playset, std::string &error);
bool remove(const Backend &b, const std::string &name, std::string &error);
bool rename(const Backend &b, const std::string &old_name, const std::string &new_name, std::string &error);
// Bytes in use by every slot (for the status and the tests).
size_t bytes_used(const Backend &b);

}  // namespace p64::content::device_playsets

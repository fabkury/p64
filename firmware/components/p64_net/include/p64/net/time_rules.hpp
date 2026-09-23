// p64 -- the rules of trusted time (ADR 0011). Only an NTP answer makes the wall clock
// trusted; the firmware's build date bounds what an answer, or a file date, may be; the
// SNTP server slots hold the server the router offers over DHCP first, then the setting,
// then two built-in fallbacks. Pure: no ESP-IDF includes (host-tested); the shell is
// clock.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p64::net::time_rules {

constexpr int64_t kDay = 86400;
// Used when the build date cannot be read (it always can; a floor must exist anyway).
constexpr int64_t kEarliestBuildDate = 1767225600;  // 2026-01-01T00:00:00Z
// The longest cache retention the settings allow (365 days) plus one day.
constexpr int64_t kLongestRetention = 366 * kDay;

// "Sep 22 2026" (the __DATE__ format of esp_app_desc_t::date; the day may be
// space-padded) -> the epoch seconds of that day's midnight UTC. False when the text is
// not such a date.
bool parse_build_date(const char *text, int64_t &epoch);
// The earliest time an NTP answer may carry: a day before the build date, since the build
// machine's local date can run up to a day ahead of UTC.
int64_t ntp_floor(int64_t build_date);
// File dates earlier than this were written under an untrusted clock (FAT stamps 1980 when
// the clock reads 1970): the NTP floor minus the longest retention, so a file played
// inside any retention window before a firmware update never counts as implausible.
int64_t file_floor(int64_t build_date);
// An NTP answer is taken only when it is not earlier than the floor.
bool accept_ntp(int64_t utc, int64_t floor);

// --- SNTP server slots ----------------------------------------------------------------

constexpr size_t kSlots = 4;
constexpr std::array<const char *, 2> kFallbacks = {"time.google.com", "time.cloudflare.com"};

// What one lwIP SNTP slot holds: a host name, or only an address (how DHCP sets it; a
// named slot may also carry the address it resolved to).
struct Slot {
  std::string name;
  bool addressed = false;
};
using Slots = std::array<Slot, kSlots>;

// What a slot should hold.
enum class Want : uint8_t { Keep, Setting, Fallback0, Fallback1, Empty };
using Plan = std::array<Want, kSlots>;

// True when the slot holds a server set by address alone: the one DHCP offered.
bool from_dhcp(const Slot &slot);
// With the router's server in slot 0 (and `keep_dhcp`): [Keep, Setting, Fallback0,
// Fallback1]; otherwise [Setting, Fallback0, Fallback1, Empty]. A fallback equal to the
// setting (case-insensitive) is not repeated; the rest move up.
Plan plan(const Slots &actual, const std::string &setting, bool keep_dhcp);
// Does the slot hold what the plan wants?
bool matches(const Slot &slot, Want want, const std::string &setting);

struct Repair {
  size_t slot;
  Want want;
};
// The slots to rewrite so that `actual` follows the plan. Every DHCP lease renewal that
// carries an NTP server clears slots 1..3 (lwIP), so the shell runs this periodically.
std::vector<Repair> repairs(const Slots &actual, const std::string &setting, bool keep_dhcp);

// While the time is not trusted and Wi-Fi is up: is a "still waiting for NTP" warning due?
// At 60 s after the connection, then every 30 minutes. `warned_at_s` is when the last
// warning went out (seconds since the connection, -1 none).
bool wait_warning_due(int64_t connected_s, int64_t warned_at_s);

}  // namespace p64::net::time_rules

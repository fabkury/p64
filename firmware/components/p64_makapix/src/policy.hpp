// The Makapix worker's policy as pure code (no ESP-IDF include; host-tested in
// tests/host/unit/makapix.cpp). fetcher.cpp does the network and card I/O and asks
// these for every decision: which channel to serve, when a paused walk starts over, when
// a page ends a walk and when its first pages go straight into an empty index, how long
// a failed refresh waits, which entry downloads next, and what happens to a job that
// arrives while the device is offline. Review of 2026-09-22, proposal P-T1.
//
// The channel templates work on any type with the fields they name, so the firmware's
// Channel (which also holds a network session) and the tests' plain structs share them.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "p64/content/makapix_index.hpp"

namespace p64::makapix::policy {

constexpr int64_t kSecond = 1000000;
constexpr int64_t kRetryBaseUs = 30 * kSecond;
constexpr int64_t kRetryMaxUs = 15 * 60 * kSecond;
constexpr int64_t kWalkIdleUs = 60 * kSecond;  // a walk paused longer (its channel left the playset) restarts

// The channel the worker serves next: a walk in progress first (one page per step), then
// a channel whose index was never loaded, then one whose refresh is due and not waiting
// out a failure. Fields: active, refreshing, loaded, retry_at_us, next_refresh_us.
template <typename Channel>
Channel *next_channel_needing_service(const std::vector<std::unique_ptr<Channel>> &channels, int64_t now_us) {
  for (const auto &ch : channels) {
    if (ch->active && ch->refreshing) return ch.get();
  }
  for (const auto &ch : channels) {
    if (!ch->active) continue;
    if (!ch->loaded) return ch.get();
    if (ch->retry_at_us && now_us < ch->retry_at_us) continue;
    if (ch->next_refresh_us == 0 || now_us >= ch->next_refresh_us) return ch.get();
  }
  return nullptr;
}

// After an index loads from the card: when its first refresh is due (0 = at once).
int64_t next_refresh_after_load(uint32_t age_s, uint32_t interval_s, int64_t now_us);

// A walk step begins: whether it starts a new walk. A walk resumed after more than
// kWalkIdleUs (its channel left the playset meanwhile) starts over, since its kept-alive
// connection has died and its cursor may be stale (ESP_ERR_HTTP_WRITE_DATA, 2026-09-21).
struct StepStart {
  bool first;  // clear the walk state and open a new session
  bool stale;  // it restarts because it paused too long
};
StepStart begin_step(bool refreshing, int64_t walk_last_us, int64_t now_us);

// Drops the entries over the size limit (the promoted feed has no size filter).
// Returns how many were dropped.
size_t drop_oversized(content::MakapixEntries &page, uint16_t max_side);

// After a page is appended to the walk: whether the walk is done, and whether the pages
// so far should be installed at once because the index is empty (downloads and playback
// then start within seconds instead of after the whole walk, M6).
struct PageOutcome {
  bool done;
  bool install_now;
};
PageOutcome after_page(size_t walked, size_t cap, bool more, bool listed_none, bool index_empty);

// How long a failed refresh waits: 30 s, doubling per consecutive failure, capped at
// 15 min; "needs pairing" waits the cap at once.
int64_t retry_delay_us(uint32_t fail_streak, const std::string &error);

// The next entry to download, round-robin across the active, loaded channels, each
// channel from its own cursor; skips cached, missing and rejected entries. Advances the
// cursor and the round-robin position. Fields: active, loaded, entries, download_cursor.
template <typename Channel>
Channel *next_download(const std::vector<std::unique_ptr<Channel>> &channels, size_t &round_robin, size_t &index) {
  const size_t n = channels.size();
  for (size_t k = 0; k < n; ++k) {
    Channel &ch = *channels[(round_robin + k) % n];
    if (!ch.active || !ch.loaded || ch.entries.empty()) continue;
    for (size_t t = 0; t < ch.entries.size(); ++t) {
      const size_t i = (ch.download_cursor + t) % ch.entries.size();
      const content::MakapixEntry &e = ch.entries[i];
      if (e.flags & (content::kMakapixCached | content::kMakapixMissing | content::kMakapixRejected)) continue;
      index = i;
      ch.download_cursor = static_cast<uint32_t>((i + 1) % ch.entries.size());
      round_robin = (round_robin + k + 1) % n;
      return &ch;
    }
  }
  return nullptr;
}

// A job taken from the queue: run it, park it until the network is up, or fail it.
// Likes run offline (they fail on their own); a Followed request made before Wi-Fi joins
// at boot is parked rather than failed, or the saved Followed playset never comes back
// (2026-09-21); anything else fails with "offline".
enum class JobDisposition : uint8_t { Run, Park, Fail };
JobDisposition job_disposition(bool online, bool is_like, bool is_followed, bool someone_waits);

// The nightly cache sweep (spec 5.4, ADR 0010, ADR 0011), one verdict per file. Delete: not
// played (its mtime touched) for longer than `older_than_s`, or dated before `floor`
// (written under an untrusted clock: FAT stamps 1980 when the clock reads 1970). Keep:
// played within the retention, or up to a day in the future (FAT's two-second rounding
// right after a touch, a clock stepped back by NTP; before the review of 2026-09-22 the
// unsigned difference wrapped and such a file was deleted as ancient). Suspect: more than
// a day in the future. The time is trusted (NTP) when a sweep runs, so a file from the
// future means the card or the clock is not what it seems: the sweep then deletes nothing.
enum class SweepVerdict : uint8_t { Keep, Delete, Suspect };
SweepVerdict sweep_verdict(int64_t mtime, int64_t now, uint32_t older_than_s, int64_t floor);

}  // namespace p64::makapix::policy

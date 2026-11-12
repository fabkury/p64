// p64 -- the memory window (ADR 0018): the artwork files a cardless p64 keeps in PSRAM.
// One window, owned by the content layer and shared by every provider (decision D23):
// bounded in files and bytes, filled once, then renewed by at most one new file per swap
// in place of the least recently played, no faster than one a minute (60 an hour, D16).
//
// The rules (docs/testing-strategy/no-card-mode.md, Q3):
//  - one copy: a file's bytes are shared with whoever plays them, never copied; a file
//    that is evicted while it plays stays alive until the player lets go;
//  - truthful: `has()` is the only source of "this file is at hand"; whoever keeps a flag
//    about a file clears it from the eviction listener, in the same step;
//  - the files played most recently (kProtected of them) are never evicted, so "previous"
//    finds them;
//  - play-this files live here too and count like any other.
//
// Keys are the part of a "mem:<key>" path after the prefix. Pure: no ESP-IDF include,
// host-tested (tests/host/unit/memory_window.cpp). Thread-safe; listeners are called
// outside the lock, on the caller's task.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace p64::content {

using FileBytes = std::shared_ptr<const std::vector<uint8_t>>;

class MemoryWindow {
 public:
  static constexpr size_t kDefaultFiles = 48;
  static constexpr size_t kDefaultBytes = 6 * 1024 * 1024;
  static constexpr size_t kProtected = 8;                   // most recently played, never evicted
  static constexpr int64_t kRenewEveryUs = 60 * 1000000LL;  // one renewal a minute: 60 an hour

  struct Stats {
    size_t files = 0, bytes = 0;
    size_t capacity_files = 0, capacity_bytes = 0;
    uint32_t stored = 0;     // files put since boot
    uint32_t evictions = 0;  // files that left to make room
    uint32_t refused = 0;    // files that could not be kept (too large, or only protected files to evict)
    uint32_t hits = 0, misses = 0;  // take() of a file that was, was not, there
    uint32_t renewals = 0;   // files admitted by the per-swap rule after the first fill
    bool filled = false;     // the first fill is over: new files come one per swap
  };

  explicit MemoryWindow(size_t files = kDefaultFiles, size_t bytes = kDefaultBytes);

  // Stores a file (replacing one of the same key), evicting the least recently played
  // files that are not protected until it fits. False when it cannot be kept; the bytes
  // are then dropped. Play-this files are put without a claim.
  bool put(const std::string &key, std::vector<uint8_t> &&bytes);
  bool has(const std::string &key) const;
  // The bytes, for playing now: the file becomes the most recently played. Null when it
  // is not (or no longer) here.
  FileBytes take(const std::string &key);
  void remove(const std::string &key);
  void clear();

  // The pace (D16). The show reports every artwork swap; a provider's worker claims a
  // download before it fetches a file for a channel. During the first fill every claim is
  // granted; afterwards one per swap, no sooner than kRenewEveryUs after the last, and to
  // one claimant only: the workers of several providers share the one window, and each
  // asking "may I?" on its own made three downloads of one swap (2026-11-12). A worker
  // that claimed and then found nothing to download gives the claim back.
  void note_swap(int64_t now_us);
  bool claim_download();
  void unclaim_download();
  // Whether a claim would be granted now (the status; a worker claims instead).
  bool admits_download(int64_t now_us) const;

  // Called with the key of every file that left without being asked to (eviction, or a
  // put that replaced nothing and was refused is not reported). Listeners live forever.
  void on_evicted(std::function<void(const std::string &key)> listener);

  Stats stats() const;

 private:
  struct File {
    std::string key;
    FileBytes bytes;
    uint64_t order = 0;   // insertion or last play, whichever is later: the eviction order
    uint64_t played = 0;  // last play (0: never): the protection order
  };
  bool is_protected(const File &f) const;  // lock held
  void note_filled();                      // lock held

  mutable std::mutex mutex_;
  std::vector<File> files_;
  std::vector<std::function<void(const std::string &)>> listeners_;
  size_t capacity_files_, capacity_bytes_;
  size_t bytes_ = 0;
  uint64_t clock_ = 0;
  bool filled_ = false;
  bool swap_pending_ = false;
  bool claimed_ = false;  // the pending renewal was claimed and not given back
  int64_t last_renewal_us_ = 0;
  bool renewed_once_ = false;
  mutable Stats stats_;
};

// The device's one window.
MemoryWindow &memory_window();

// "mem:<key>" paths.
inline bool is_memory_path(const std::string &path) { return path.rfind("mem:", 0) == 0; }
inline std::string memory_key(const std::string &path) { return is_memory_path(path) ? path.substr(4) : std::string(); }

}  // namespace p64::content

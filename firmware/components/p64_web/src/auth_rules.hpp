// The PIN's rules as pure code (spec 10.3; host-tested in tests/host/unit/web.cpp; review
// of 2026-09-22, P-T2): what a valid PIN is, the lockout after five wrong PINs, and the
// table of eight sessions with least-recently-used eviction. auth.cpp keeps the hashing
// (mbedtls), the NVS store and the HTTP handlers, and passes the clock and the random
// bytes in.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace p64::web::auth::rules {

// 4 to 8 digits.
bool valid_pin(const std::string &pin);

// Five wrong PINs in a row lock authentication for 30 s; a right one resets the count.
// While locked, even the right PIN is refused and nothing is counted.
class Lockout {
 public:
  static constexpr int kMaxFailures = 5;
  static constexpr int64_t kLockoutUs = 30LL * 1000 * 1000;

  // Seconds of lockout left, rounded up (0 when open).
  int locked_seconds(int64_t now_us) const;
  // An attempt whose PIN was right or wrong; returns whether it is accepted. Sets
  // `just_locked` when this attempt started a lockout.
  bool attempt(bool right, int64_t now_us, bool *just_locked = nullptr);

 private:
  int failures_ = 0;
  int64_t locked_until_us_ = 0;
};

// Eight sessions, each a 32-hex-digit token; a new one takes a free slot or evicts the
// least recently seen.
class Sessions {
 public:
  static constexpr int kMax = 8;
  // A new session from 16 random bytes; returns its token.
  const char *create(const uint8_t random[16], int64_t now_us);
  // True (and the session refreshed) when `token` is a live session.
  bool touch(const char *token, int64_t now_us);
  void end(const char *token);
  void end_all();
  int count() const;

 private:
  struct Slot {
    char token[33] = {};
    int64_t last_seen_us = 0;
  };
  Slot *find(const char *token);
  Slot slots_[kMax];
};

}  // namespace p64::web::auth::rules

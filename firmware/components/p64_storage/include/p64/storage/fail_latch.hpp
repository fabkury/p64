// p64 -- when a mounted card counts as failed (ADR 0018, decision D19). There is no
// card-detect line: a card pulled out, or one that died, is noticed only by its errors.
// The rule: a write that fails for any reason but a full card trips the latch at once;
// reads trip it after three I/O errors in a row (a file that is simply not there is not
// an error, and a read that works clears the count). Once tripped the latch stays so
// until the restart: the device goes on as cardless and does not look at the card again.
//
// Pure: no ESP-IDF include; host-tested (tests/host/unit/system.cpp).
#pragma once

#include <cstdint>

namespace p64::storage {

class FailLatch {
 public:
  static constexpr uint8_t kReadErrors = 3;

  // Each returns true when this call tripped the latch.
  bool read_ok() {
    read_errors_ = 0;
    return false;
  }
  bool read_error() {
    if (tripped_) return false;
    if (++read_errors_ < kReadErrors) return false;
    tripped_ = true;
    return true;
  }
  bool write_error(bool card_full) {
    if (tripped_ || card_full) return false;
    tripped_ = true;
    return true;
  }
  bool tripped() const { return tripped_; }
  uint8_t read_errors() const { return read_errors_; }

 private:
  bool tripped_ = false;
  uint8_t read_errors_ = 0;
};

}  // namespace p64::storage

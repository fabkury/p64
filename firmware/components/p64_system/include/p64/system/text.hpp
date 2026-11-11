// p64 -- text that did not originate in the firmware, made safe to put into a JSON
// document. Pure (no ESP-IDF include), host-tested.
#pragma once

#include <cstddef>
#include <string>

namespace p64::system {

// The printable ASCII of a fixed-size field that may hold anything: it stops at the
// first NUL or at `size`, and every byte outside 0x20..0x7E becomes '?'. For names read
// back from memory that may be corrupt: a core dump's task name after a stack overflow
// was raw bytes, and the status document that carried it was no longer valid UTF-8
// (finding F-03, 2026-11-11).
inline std::string printable_ascii(const char *data, size_t size) {
  std::string out;
  if (!data) return out;
  for (size_t i = 0; i < size && data[i] != '\0'; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    out += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
  }
  return out;
}

}  // namespace p64::system

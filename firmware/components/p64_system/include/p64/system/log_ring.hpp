// p64 -- the log ring buffer: every log line also lands in a ring in PSRAM so the web
// UI's diagnostics can show the recent log without a serial cable (spec 15.5).
#pragma once

#include <cstddef>
#include <string>

namespace p64::system::logring {

// Installs the hook. `bytes` is the ring size (PSRAM); call once, early.
void init(size_t bytes);
// The most recent `max_bytes` of the log, oldest first.
std::string tail(size_t max_bytes);
// Bytes written since boot (for "new since" polling).
size_t total_written();

}  // namespace p64::system::logring

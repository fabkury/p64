// p64 -- download speed test: measures application-level HTTP(S) throughput while
// the show keeps running, and logs the results. Off unless P64_SPEEDTEST is set, but
// the web control can run it on demand as a heavy TLS load (/debug/stress).
#pragma once

#include <cstdint>

namespace p64::speedtest {

// Schedules the boot-time run (45 s after boot, on the Wi-Fi core) when P64_SPEEDTEST
// is set. No-op otherwise.
void start();

// Runs the test now, `loops` times back to back, on the Wi-Fi core (used by the web
// control's /debug/stress as a heavy TLS load). False when a run is already going.
bool run_now(uint32_t loops);
bool running();

}  // namespace p64::speedtest

// p64 -- download speed test: measures application-level HTTP(S) throughput while
// the show keeps running, and logs the results. Off unless P64_SPEEDTEST is set.
#pragma once

namespace p64::speedtest {

// Starts the test task (runs once, 45 s after boot, on the Wi-Fi core). No-op when
// disabled in menuconfig.
void start();

}  // namespace p64::speedtest

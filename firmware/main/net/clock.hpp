// p64 -- wall clock: local time from NTP (SNTP over the Wi-Fi link).
#pragma once

namespace p64::clock {

// Sets the timezone rule and starts SNTP against `ntp_server`. Time arrives once
// Wi-Fi is up; until then synced() is false.
void start(const char *tz, const char *ntp_server);

// True after the first successful NTP sync.
bool synced();

// Local time of day. Returns false when the time is not synced yet.
bool local_time(int &hour, int &minute, int &second);

}  // namespace p64::clock

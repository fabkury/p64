// p64 -- wall clock: the on-board RTC seeds the system time at boot, SNTP over Wi-Fi
// corrects both, the time zone comes from the IANA table. Until the time is known,
// local_time() says so (clock features show "--:--" and Makapix TLS waits).
#pragma once

#include <ctime>
#include <string>

namespace p64::net::clock {

// Seeds the time from the RTC when it has one, applies the zone and starts SNTP against
// the server (SNTP waits for the network).
void start(const std::string &ntp_server, const std::string &iana_zone);
// Changes the zone or the server at runtime.
void set_timezone(const std::string &iana_zone);
void set_ntp_server(const std::string &server);
// Sets the time by hand (the web UI's "set time from this browser"); written to the RTC.
void set_manual(time_t utc);
// True once the RTC, NTP or a manual set has delivered the time.
bool synced();
// "none", "rtc", "ntp" or "manual": where the current time came from.
const char *source();
// Local broken-down time; false while the time is unknown.
bool local_time(struct tm &out);
// The POSIX rule in force, for status.
std::string posix_rule();

}  // namespace p64::net::clock

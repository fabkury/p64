// p64 -- wall clock: SNTP over Wi-Fi, the time zone from the IANA table, later the
// on-board RTC. Until the time is known, local_time() says so (clock features show
// "--:--" and Makapix TLS waits).
#pragma once

#include <ctime>
#include <string>

namespace p64::net::clock {

// Applies the zone and starts SNTP against the server (SNTP waits for the network).
void start(const std::string &ntp_server, const std::string &iana_zone);
// Changes the zone or the server at runtime.
void set_timezone(const std::string &iana_zone);
void set_ntp_server(const std::string &server);
// Sets the time by hand (the web UI's "set time from this browser").
void set_manual(time_t utc);
// True once NTP (or a manual set) has delivered the time.
bool synced();
// Local broken-down time; false while the time is unknown.
bool local_time(struct tm &out);
// The POSIX rule in force, for status.
std::string posix_rule();

}  // namespace p64::net::clock

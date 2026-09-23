// p64 -- wall clock. The time is trusted only once an NTP server has answered in this boot
// (ADR 0011): the on-board RTC has no battery and is not used, and there is no manual
// set. SNTP asks the server the router offers over DHCP, then the setting, then two
// built-in fallbacks, at boot, on every Wi-Fi connection and every 6 hours; an answer
// earlier than the firmware's build date is refused. Until the first answer, local_time()
// and now_utc() say the time is unknown (clocks show "--:--"; Makapix, the night schedule
// and the cache sweep wait). Once trusted, the time stays trusted until the next reboot.
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace p64::net::clock {

// Applies the zone and starts SNTP (which waits for the network itself).
void start(const std::string &ntp_server, const std::string &iana_zone);
// Changes the zone or the server at runtime; a new server is asked at once.
void set_timezone(const std::string &iana_zone);
void set_ntp_server(const std::string &server);
// True once NTP has answered in this boot.
bool synced();
// "ntp" once synced, else "none".
const char *source();
// The trusted time: false (and `out` untouched) until NTP has answered.
bool now_utc(time_t &out);
// Local broken-down trusted time; false while the time is unknown.
bool local_time(struct tm &out);
// The POSIX rule in force, for status.
std::string posix_rule();
// File modification times earlier than this (epoch seconds) were written under an
// untrusted clock (ADR 0011: the build date minus the longest cache retention).
int64_t file_date_floor();

struct Server {
  std::string host;   // the name, or the address DHCP gave
  bool dhcp = false;  // offered by the router
  uint8_t reach = 0;  // lwIP's reachability register: bit 0 = the last poll was answered
};
struct Status {
  bool synced = false;
  int64_t last_sync_s = -1;  // seconds since the last accepted answer, -1 never
  uint32_t syncs = 0;        // accepted answers in this boot
  uint32_t rejected = 0;     // answers refused (earlier than the build date)
  int64_t waiting_s = -1;    // while not synced and Wi-Fi is up: seconds since the connection
  uint32_t interval_s = 0;   // the re-sync interval
  std::vector<Server> servers;
};
Status status();

}  // namespace p64::net::clock

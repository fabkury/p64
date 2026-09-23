// p64 -- Wi-Fi: one saved network, station mode with reconnection, and setup mode (the
// open p64-setup access point with a captive portal) after 60 s without a connection,
// while the saved network keeps being retried underneath (spec 10.1). Credentials live
// in the NVS namespace "wifi"; a development build may seed them from sdkconfig.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace p64::net::wifi {

struct Status {
  bool connected = false;
  bool setup_mode = false;
  bool network_saved = false;  // credentials are stored (a RAM copy: reading it never touches flash)
  std::string ssid;      // the saved network
  std::string ip, gateway, netmask;
  int rssi = 0;
  std::string hostname;  // "p64" or "p64-<name>"
  std::string ap_ssid;   // "p64-setup" while setup mode is on
  std::string ap_ip;     // "192.168.4.1"
};

struct ScanEntry {
  std::string ssid;
  int rssi = 0;
  bool secure = false;
};

// Brings the stack up, applies the hostname, and either connects to the saved network
// (opening setup mode after the fallback delay) or opens setup mode at once when there
// are no credentials.
bool start(const std::string &hostname);
bool has_credentials();
std::string saved_ssid();
// Stores new credentials and reconnects with them.
bool save_credentials(const std::string &ssid, const std::string &password);
// Forgets the network: the device drops into setup mode.
bool erase_credentials();
Status status();
// Blocking scan (a few seconds); sorted by signal, one entry per SSID.
std::vector<ScanEntry> scan();
// Changes the mDNS/DHCP hostname; takes full effect on the next connection.
void set_hostname(const std::string &hostname);

}  // namespace p64::net::wifi

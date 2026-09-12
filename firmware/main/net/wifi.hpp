// p64 -- Wi-Fi station: joins the configured network and keeps reconnecting.
#pragma once

namespace p64::wifi {

// Initialises NVS, the network stack and Wi-Fi, then connects in the background.
// Returns false (and logs why) when no SSID is configured or initialisation fails.
bool start(const char *ssid, const char *password);

// True while the station has an IP address.
bool connected();

}  // namespace p64::wifi

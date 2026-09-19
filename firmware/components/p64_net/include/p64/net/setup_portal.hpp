// p64 -- the setup portal: the streamlined Wi-Fi page served in setup mode, the captive
// probe redirects, the scan list, and the save/erase actions (spec 10.1).
#pragma once

#include "esp_http_server.h"

namespace p64::net::portal {

// Registers the portal routes on the HTTP server. "/" is served by the portal only
// while setup mode is on; otherwise it falls through to the handler given (nullptr:
// a plain "p64 is running" page until the web UI exists).
void init(esp_err_t (*root_when_running)(httpd_req_t *));

}  // namespace p64::net::portal

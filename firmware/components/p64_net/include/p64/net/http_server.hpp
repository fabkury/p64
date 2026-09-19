// p64 -- the one HTTP server of the firmware (port 80, core 0). Modules register their
// routes on it: the setup portal, and the API and web UI (p64_web).
#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "esp_http_server.h"

namespace p64::net::http {

bool start();
httpd_handle_t handle();
// Registers a route; logs and returns false on failure. Routes pass through the gate
// (below) unless `open`; a WebSocket route is gated at its handshake only.
bool add(const httpd_uri_t &uri, bool open = false);
// The gate every non-open route goes through (the PIN, spec 10.3): returns true to let
// the request proceed; when it returns false it has already sent the refusal.
using Gate = std::function<bool(httpd_req_t *)>;
void set_gate(Gate gate);
// Sends a small text or JSON body with the status and content type given.
esp_err_t send(httpd_req_t *req, const char *status, const char *content_type, const char *body);
// Reads the whole request body (up to max_len) into `out`; false when it is larger.
bool read_body(httpd_req_t *req, std::string &out, size_t max_len);

}  // namespace p64::net::http

// p64 -- one HTTP(S) request at a time: GET or POST with headers, a size cap, manual
// redirects and the certificate bundle. Every Makapix call and every artwork download
// goes through here, so the User-Agent (spec 13) and the buffer sizes live in one place.
// Blocking; call from core-0 tasks only (the fetcher, the HTTP server's handlers).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "esp_http_client.h"

namespace p64::net::fetch {

struct Request {
  std::string url;
  const char *method = "GET";  // "GET" or "POST"
  std::string body;            // sent with POST
  const char *content_type = "application/json";
  std::vector<std::pair<std::string, std::string>> headers;  // extra request headers
  uint32_t timeout_ms = 15000;
  size_t max_bytes = 256 * 1024;  // the response body cap; larger answers fail with ESP_ERR_INVALID_SIZE
  bool follow_redirects = true;
};

struct Result {
  int status = 0;              // HTTP status, 0 when the transport failed
  esp_err_t error = ESP_OK;    // transport or size error
  std::vector<uint8_t> body;   // the response body (large ones land in PSRAM)
  int64_t content_length = -1;
  uint32_t took_ms = 0;
};

// Performs one request on a fresh connection; true when an HTTP status came back.
bool perform(const Request &request, Result &out);

// A connection kept open between requests to the same host (keep-alive): a page walk
// or a run of downloads pays the TCP and TLS set-up once. Close it when the run ends
// so the memory goes back.
class Session {
 public:
  Session() = default;
  ~Session() { close(); }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  bool perform(const Request &request, Result &out);
  void close();
  bool open() const { return client_ != nullptr; }

 private:
  esp_http_client_handle_t client_ = nullptr;
  std::string host_;  // scheme and host the client was created for
  bool holds_tls_ = false;
};

// "p64/<firmware version>" (spec 13).
const char *user_agent();
// The body as a string (for JSON).
std::string body_string(const Result &r);

}  // namespace p64::net::fetch

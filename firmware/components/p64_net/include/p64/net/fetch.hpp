// p64 -- one HTTP(S) request at a time: GET or POST with headers, a size cap, manual
// redirects and the certificate bundle. Every Makapix call and every artwork download
// goes through here, so the User-Agent (spec 13) and the buffer sizes live in one place.
// Blocking; call from core-0 tasks only (the fetcher, the HTTP server's handlers).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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
  // Called as the body arrives (received bytes, content length or -1), for progress.
  std::function<void(size_t, int64_t)> progress;
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

// The one-TLS-session-at-a-time slot (ADR 0009), for code that runs its own HTTPS
// client (the updater): hold it for the whole transfer. Recursive, and handed to its
// waiters first come, first served (src/tls_slot.hpp, 2026-09-28).
void tls_lock();
void tls_unlock();
// True while another task waits for the slot: a holder with a keep-alive session and
// nothing in flight closes it, so the other gets its turn (a page walk yields between
// pages, an idle download session at once instead of after its idle time).
bool tls_waiting();
// For the status document: who holds the slot and how the queue has behaved.
struct SlotStatus {
  bool held = false;
  const char *holder = "";  // the holding task's name ("" when free)
  uint32_t depth = 0;
  uint32_t waiters = 0;
  uint32_t grants = 0;
  uint32_t waits = 0;
  uint32_t handoffs = 0;
  uint32_t max_wait_ms = 0;
};
SlotStatus tls_status();

// "p64/<firmware version>" (spec 13).
const char *user_agent();
// The body as a string (for JSON).
std::string body_string(const Result &r);

}  // namespace p64::net::fetch

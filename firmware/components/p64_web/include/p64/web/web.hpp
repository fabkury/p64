// p64 -- the web layer: /api/v1 routes on the one HTTP server, a WebSocket that pushes
// status, the live preview, the file manager, playsets, Makapix and the embedded UI.
// The application hands it hooks for everything that belongs to the show and the
// display, so this component depends on no application code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "cJSON.h"
#include "esp_http_server.h"
#include "p64/display/display.hpp"
#include "p64/gfx/frame.hpp"

namespace p64::web {

struct Hooks {
  // Show commands (queued to the main task; they return at once).
  std::function<void()> next;
  std::function<void()> previous;
  std::function<void()> pause;
  std::function<void()> resume;
  std::function<void()> reset_timer;
  std::function<void()> refresh;
  std::function<void(size_t history_index)> go_to;
  std::function<bool(const std::string &absolute_path, std::string &error)> play_file;
  std::function<bool(const std::string &sqid_or_url, std::string &error)> play_post;  // Makapix post
  std::function<bool(const std::string &url, std::string &error)> play_url;           // arbitrary URL
  std::function<bool(int32_t post_id, bool liked, std::string &error)> like;          // blocks briefly
  std::function<bool(const std::string &name, std::string &error)> activate_playset;
  // Snapshots: each returns a new object the caller owns.
  std::function<cJSON *()> playback_status;  // the status document's "playback" object
  std::function<cJSON *()> channels;
  std::function<cJSON *()> history;
  std::function<cJSON *()> playsets;
  // Display.
  std::function<bool(gfx::Frame &)> snapshot;  // the frame on the panel (live preview)
  std::function<void(display::Mode)> request_mode;
  std::function<display::Display *()> display;
};

// Registers every route and starts the WebSocket push task. Call after the HTTP server.
void init(const Hooks &hooks);
const Hooks &hooks();
// Pushes a status update to the WebSocket clients now (otherwise every 2 s).
void notify();
// Builds the status document (also used by the WebSocket push).
cJSON *build_status();
// The UI page handler, for the setup portal to fall through to when not in setup mode.
esp_err_t ui_handler(httpd_req_t *req);

// Shared helpers for route handlers.
esp_err_t reply_ok(httpd_req_t *req, cJSON *data);  // takes ownership of data (may be null)
esp_err_t reply_error(httpd_req_t *req, const char *status, const char *code, const std::string &message);
// Query parameter, URL-decoded; false when absent.
bool query_param(httpd_req_t *req, const char *name, std::string &out);
// Parses a JSON body (up to 32 KB); nullptr with an error reply already sent when invalid.
cJSON *parse_body(httpd_req_t *req);

}  // namespace p64::web

#include "p64/net/http_server.hpp"

#include <string>
#include <vector>

#include "esp_log.h"
#include "p64/net/origin.hpp"

namespace p64::net::http {

esp_err_t after_refusal(httpd_req_t *req);

namespace {

constexpr const char *TAG = "http";
httpd_handle_t g_server = nullptr;
Gate g_gate;

// What add() keeps per route: the real handler behind the gate trampoline. Routes live
// for the whole run, so the entries are never freed.
struct Route {
  esp_err_t (*handler)(httpd_req_t *);
  void *user_ctx;
  bool websocket;
};

// A request header's value ("" when absent or longer than 255 bytes).
std::string header(httpd_req_t *req, const char *name) {
  const size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0 || len > 255) return "";
  std::string value(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) return "";
  value.resize(len);
  return value;
}

esp_err_t refuse_foreign_origin(httpd_req_t *req) {
  send(req, "403 Forbidden", "application/json",
       "{\"ok\":false,\"error\":\"this request comes from another site\",\"code\":\"FOREIGN_ORIGIN\"}");
  return after_refusal(req);
}

esp_err_t gated(httpd_req_t *req) {
  auto *route = static_cast<Route *>(req->user_ctx);
  // A WebSocket handler is also called for every frame (method 0); only the handshake
  // (a GET with headers) is checked.
  const bool handshake = !route->websocket || req->method == HTTP_GET;
  // A browser on another site can make its visitor's browser send requests here; it
  // says so in the Origin header, which scripts and tools do not send. Whatever changes
  // the device, and the WebSocket, is refused when the Origin is not this device (ADR
  // 0019, decision D29 of docs/testing-strategy/decisions.md).
  if (handshake && (req->method != HTTP_GET || route->websocket) &&
      !same_origin(header(req, "Origin"), header(req, "Host"))) {
    // The server has already answered a WebSocket's upgrade when it calls the handler:
    // there is no status left to send, so the connection is closed instead.
    if (route->websocket) return ESP_FAIL;
    return refuse_foreign_origin(req);
  }
  if (handshake && g_gate && !g_gate(req)) return after_refusal(req);
  req->user_ctx = route->user_ctx;
  return route->handler(req);
}

// Unknown routes and wrong methods answer in the API's envelope, like every other error.
esp_err_t not_found(httpd_req_t *req, httpd_err_code_t) {
  send(req, "404 Not Found", "application/json", "{\"ok\":false,\"error\":\"no such route\",\"code\":\"NOT_FOUND\"}");
  return ESP_FAIL;  // close: whatever body came with it is not read
}

esp_err_t wrong_method(httpd_req_t *req, httpd_err_code_t) {
  send(req, "405 Method Not Allowed", "application/json",
       "{\"ok\":false,\"error\":\"this route does not take that method\",\"code\":\"METHOD_NOT_ALLOWED\"}");
  return ESP_FAIL;
}

}  // namespace

esp_err_t after_refusal(httpd_req_t *req) {
  // A refused request's body is not read. With a small one the connection can go on;
  // with a large one (an upload) the server would otherwise drain it 32 bytes at a
  // time while every other client waits (27 s for 8 MB, finding F-04): close instead.
  return req->content_len > 4096 ? ESP_FAIL : ESP_OK;
}

bool start() {
  if (g_server) return true;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.core_id = 0;
  cfg.stack_size = 8192;
  cfg.max_uri_handlers = 96;  // the API, the portal and the UI register about 70 routes
  cfg.max_open_sockets = 12;  // needs CONFIG_LWIP_MAX_SOCKETS >= 15 (three are the server's own)
  cfg.lru_purge_enable = true;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  // One task serves every request in turn (ADR 0019), so a stalled client holds the
  // server for at most this long: 5 s each way since 2026-11-11 (10 before; ten
  // half-sent requests then made the device unreachable for 90 s, finding F-04).
  cfg.recv_wait_timeout = 5;
  cfg.send_wait_timeout = 5;
  const esp_err_t err = httpd_start(&g_server, &cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
    g_server = nullptr;
    return false;
  }
  httpd_register_err_handler(g_server, HTTPD_404_NOT_FOUND, not_found);
  httpd_register_err_handler(g_server, HTTPD_405_METHOD_NOT_ALLOWED, wrong_method);
  ESP_LOGI(TAG, "listening on port %d", cfg.server_port);
  return true;
}

httpd_handle_t handle() { return g_server; }

bool add(const httpd_uri_t &uri, bool open) {
  if (!g_server) return false;
  httpd_uri_t wrapped = uri;
  if (!open) {
    auto *route = new Route{uri.handler, uri.user_ctx, uri.is_websocket};
    wrapped.handler = gated;
    wrapped.user_ctx = route;
  }
  const esp_err_t err = httpd_register_uri_handler(g_server, &wrapped);
  if (err != ESP_OK) ESP_LOGE(TAG, "route %s: %s", uri.uri, esp_err_to_name(err));
  return err == ESP_OK;
}

void set_gate(Gate gate) { g_gate = std::move(gate); }

esp_err_t send(httpd_req_t *req, const char *status, const char *content_type, const char *body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, content_type);
  return httpd_resp_sendstr(req, body);
}

bool read_body(httpd_req_t *req, std::string &out, size_t max_len) {
  out.clear();
  if (req->content_len > max_len) return false;
  out.resize(req->content_len);
  size_t got = 0;
  while (got < out.size()) {
    const int n = httpd_req_recv(req, out.data() + got, out.size() - got);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace p64::net::http

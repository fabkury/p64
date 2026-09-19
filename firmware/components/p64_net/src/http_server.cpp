#include "p64/net/http_server.hpp"

#include <string>
#include <vector>

#include "esp_log.h"

namespace p64::net::http {
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

esp_err_t gated(httpd_req_t *req) {
  auto *route = static_cast<Route *>(req->user_ctx);
  // A WebSocket handler is also called for every frame (method 0); only the handshake
  // (a GET with headers) is checked.
  const bool handshake = !route->websocket || req->method == HTTP_GET;
  if (handshake && g_gate && !g_gate(req)) return ESP_OK;
  req->user_ctx = route->user_ctx;
  return route->handler(req);
}

}  // namespace

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
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 10;
  const esp_err_t err = httpd_start(&g_server, &cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
    g_server = nullptr;
    return false;
  }
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

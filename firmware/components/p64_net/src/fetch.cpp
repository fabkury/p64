#include "p64/net/fetch.hpp"

#include <cstring>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace p64::net::fetch {
namespace {

constexpr const char *TAG = "fetch";
constexpr int kMaxRedirects = 4;
char g_user_agent[48] = {};

// One transient TLS session at a time (ADR 0009): HTTPS requests queue here; a Session
// on HTTPS holds the slot from its first request until it closes. Recursive, so a task
// that keeps a session open can still make one-shot requests.
SemaphoreHandle_t g_tls_slot = nullptr;

bool is_https(const std::string &url) { return url.rfind("https://", 0) == 0; }

void tls_take() {
  if (!g_tls_slot) g_tls_slot = xSemaphoreCreateRecursiveMutex();
  xSemaphoreTakeRecursive(g_tls_slot, portMAX_DELAY);
}

void tls_give() {
  if (g_tls_slot) xSemaphoreGiveRecursive(g_tls_slot);
}

std::string scheme_and_host(const std::string &url) {
  const size_t s = url.find("://");
  if (s == std::string::npos) return "";
  const size_t p = url.find('/', s + 3);
  return p == std::string::npos ? url : url.substr(0, p);
}

esp_http_client_handle_t make_client(const Request &request, bool keep_alive) {
  esp_http_client_config_t cfg = {};
  cfg.url = request.url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = static_cast<int>(request.timeout_ms);
  cfg.user_agent = user_agent();
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1536;
  cfg.disable_auto_redirect = true;  // followed by hand below (perform() would buffer)
  cfg.keep_alive_enable = keep_alive;
  return esp_http_client_init(&cfg);
}

// Runs one request on an open client. `owned` clients are closed on transport errors
// so the next request starts clean; kept ones keep the socket for the next call.
bool run(esp_http_client_handle_t client, const Request &request, Result &out, bool keep) {
  out = Result{};
  const int64_t t0 = esp_timer_get_time();
  const bool post = std::strcmp(request.method, "POST") == 0;
  esp_http_client_set_url(client, request.url.c_str());
  esp_http_client_set_method(client, post ? HTTP_METHOD_POST : HTTP_METHOD_GET);
  esp_http_client_set_timeout_ms(client, static_cast<int>(request.timeout_ms));
  for (const auto &h : request.headers) esp_http_client_set_header(client, h.first.c_str(), h.second.c_str());
  if (post) esp_http_client_set_header(client, "Content-Type", request.content_type);
  esp_http_client_set_header(client, "Connection", keep ? "keep-alive" : "close");

  int status = 0;
  int64_t content_length = 0;
  bool opened = false;
  for (int hop = 0;; ++hop) {
    esp_err_t err = esp_http_client_open(client, post ? static_cast<int>(request.body.size()) : 0);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "%s %s: open failed: %s", request.method, request.url.c_str(), esp_err_to_name(err));
      esp_http_client_close(client);
      out.error = err;
      return false;
    }
    opened = true;
    if (post && !request.body.empty()) {
      const int written = esp_http_client_write(client, request.body.data(), static_cast<int>(request.body.size()));
      if (written < static_cast<int>(request.body.size())) {
        ESP_LOGW(TAG, "POST %s: short write (%d of %u)", request.url.c_str(), written,
                 static_cast<unsigned>(request.body.size()));
        esp_http_client_close(client);
        out.error = ESP_FAIL;
        return false;
      }
    }
    content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
      ESP_LOGW(TAG, "%s %s: no response headers", request.method, request.url.c_str());
      esp_http_client_close(client);
      out.error = ESP_FAIL;
      return false;
    }
    status = esp_http_client_get_status_code(client);
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (!redirect || !request.follow_redirects || hop >= kMaxRedirects) break;
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    esp_http_client_close(client);
    if (post && status == 303) esp_http_client_set_method(client, HTTP_METHOD_GET);
  }
  out.status = status;
  out.content_length = content_length;
  if (content_length > static_cast<int64_t>(request.max_bytes)) {
    ESP_LOGW(TAG, "%s: %lld bytes exceed the %u byte cap", request.url.c_str(), static_cast<long long>(content_length),
             static_cast<unsigned>(request.max_bytes));
    esp_http_client_close(client);
    out.error = ESP_ERR_INVALID_SIZE;
    return true;
  }
  if (content_length > 0) out.body.reserve(static_cast<size_t>(content_length));
  uint8_t chunk[2048];
  while (true) {
    const int n = esp_http_client_read(client, reinterpret_cast<char *>(chunk), sizeof(chunk));
    if (n < 0) {
      ESP_LOGW(TAG, "%s: read failed after %u bytes", request.url.c_str(), static_cast<unsigned>(out.body.size()));
      out.error = ESP_FAIL;
      break;
    }
    if (n == 0) break;
    if (out.body.size() + static_cast<size_t>(n) > request.max_bytes) {
      out.error = ESP_ERR_INVALID_SIZE;
      break;
    }
    out.body.insert(out.body.end(), chunk, chunk + n);
  }
  if (!keep || out.error != ESP_OK || !esp_http_client_is_complete_data_received(client)) {
    esp_http_client_close(client);
  }
  (void)opened;
  if (out.error == ESP_OK && content_length > 0 && static_cast<int64_t>(out.body.size()) != content_length) {
    ESP_LOGW(TAG, "%s: got %u of %lld bytes", request.url.c_str(), static_cast<unsigned>(out.body.size()),
             static_cast<long long>(content_length));
    out.error = ESP_ERR_INVALID_RESPONSE;
  }
  out.took_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
  ESP_LOGD(TAG, "%s %s -> %d, %u bytes, %u ms", request.method, request.url.c_str(), status,
           static_cast<unsigned>(out.body.size()), out.took_ms);
  return true;
}

}  // namespace

void tls_lock() { tls_take(); }
void tls_unlock() { tls_give(); }

const char *user_agent() {
  if (!g_user_agent[0]) {
    const esp_app_desc_t *app = esp_app_get_description();
    std::snprintf(g_user_agent, sizeof(g_user_agent), "p64/%s", app ? app->version : "0");
  }
  return g_user_agent;
}

std::string body_string(const Result &r) { return std::string(r.body.begin(), r.body.end()); }

bool perform(const Request &request, Result &out) {
  const bool tls = is_https(request.url);
  if (tls) tls_take();
  esp_http_client_handle_t client = make_client(request, false);
  if (!client) {
    out = Result{};
    out.error = ESP_ERR_NO_MEM;
    if (tls) tls_give();
    return false;
  }
  const bool ok = run(client, request, out, false);
  esp_http_client_cleanup(client);
  if (tls) tls_give();
  return ok;
}

bool Session::perform(const Request &request, Result &out) {
  const std::string host = scheme_and_host(request.url);
  if (client_ && host != host_) close();
  if (!client_) {
    if (is_https(request.url)) {
      tls_take();
      holds_tls_ = true;
    }
    client_ = make_client(request, true);
    if (!client_) {
      out = Result{};
      out.error = ESP_ERR_NO_MEM;
      if (holds_tls_) {
        tls_give();
        holds_tls_ = false;
      }
      return false;
    }
    host_ = host;
  }
  return run(client_, request, out, true);
}

void Session::close() {
  if (!client_) return;
  esp_http_client_close(client_);
  esp_http_client_cleanup(client_);
  client_ = nullptr;
  host_.clear();
  if (holds_tls_) {
    tls_give();
    holds_tls_ = false;
  }
}

}  // namespace p64::net::fetch

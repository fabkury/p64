// p64 -- the PIN (spec 10.3): 4 to 8 digits, stored as a salted SHA-256 in NVS; when set,
// every API route needs a session (cookie set by /api/v1/auth/login, or the PIN in the
// X-P64-Pin header for scripts, or "Authorization: Bearer <session>"). Five failures
// lock authentication for 30 s (429 with Retry-After). The setup portal, the UI shell
// page and the auth routes stay open; UDP streams are not covered.
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "p64/net/http_server.hpp"
#include "p64/system/flash_guard.hpp"
#include "p64/web/web.hpp"
#include "auth_rules.hpp"

namespace p64::web {

esp_err_t reply_ok(httpd_req_t *req, cJSON *data);
esp_err_t reply_error(httpd_req_t *req, const char *status, const char *code, const std::string &message);
cJSON *parse_body(httpd_req_t *req);

namespace auth {
namespace {

constexpr const char *TAG = "auth";
constexpr const char *kNamespace = "p64auth";
constexpr const char *kCookie = "p64_session";

std::mutex g_mutex;
bool g_loaded = false;
bool g_pin_set = false;
uint8_t g_salt[16], g_hash[32];
rules::Sessions g_sessions;  // the session table and the lockout: auth_rules.cpp, host-tested
rules::Lockout g_lockout;

void hash_pin(const uint8_t *salt, const std::string &pin, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, salt, 16);
  mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char *>(pin.data()), pin.size());
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

void load_impl() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return;
  size_t salt_len = sizeof(g_salt), hash_len = sizeof(g_hash);
  if (nvs_get_blob(h, "salt", g_salt, &salt_len) == ESP_OK && salt_len == sizeof(g_salt) &&
      nvs_get_blob(h, "hash", g_hash, &hash_len) == ESP_OK && hash_len == sizeof(g_hash)) {
    g_pin_set = true;
  }
  nvs_close(h);
  ESP_LOGI(TAG, "PIN %s", g_pin_set ? "set: API routes need a session" : "not set");
}

bool store_impl(const std::string &pin) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok;
  if (pin.empty()) {
    ok = nvs_erase_all(h) == ESP_OK;
    g_pin_set = false;
  } else {
    esp_fill_random(g_salt, sizeof(g_salt));
    hash_pin(g_salt, pin, g_hash);
    ok = nvs_set_blob(h, "salt", g_salt, sizeof(g_salt)) == ESP_OK && nvs_set_blob(h, "hash", g_hash, sizeof(g_hash)) == ESP_OK;
    g_pin_set = ok;
  }
  ok = nvs_commit(h) == ESP_OK && ok;
  nvs_close(h);
  return ok;
}

void load() {
  if (g_loaded) return;
  g_loaded = true;
  system::on_internal_stack([] { load_impl(); return true; });
}

bool store(const std::string &pin) { return system::on_internal_stack([&] { return store_impl(pin); }); }

using rules::valid_pin;

// Locked: how many seconds remain (0 when not locked).
int locked_seconds() { return g_lockout.locked_seconds(esp_timer_get_time()); }

// Checks a PIN under the lock; counts failures.
bool check_pin(const std::string &pin) {
  if (!g_pin_set) return true;
  if (locked_seconds() > 0) return false;
  uint8_t h[32];
  hash_pin(g_salt, pin, h);
  bool just_locked = false;
  const bool ok = g_lockout.attempt(std::memcmp(h, g_hash, sizeof(h)) == 0, esp_timer_get_time(), &just_locked);
  if (just_locked) ESP_LOGW(TAG, "%d wrong PINs: locked for 30 s", rules::Lockout::kMaxFailures);
  return ok;
}

const char *new_session() {
  uint8_t raw[16];
  esp_fill_random(raw, sizeof(raw));
  return g_sessions.create(raw, esp_timer_get_time());
}

std::string header(httpd_req_t *req, const char *name) {
  const size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0) return "";
  std::string v(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, v.data(), v.size()) != ESP_OK) return "";
  v.resize(len);
  return v;
}

std::string cookie_value(httpd_req_t *req) {
  const std::string cookies = header(req, "Cookie");
  const std::string key = std::string(kCookie) + "=";
  size_t pos = 0;
  while (pos < cookies.size()) {
    while (pos < cookies.size() && (cookies[pos] == ' ' || cookies[pos] == ';')) ++pos;
    if (cookies.compare(pos, key.size(), key) == 0) {
      const size_t start = pos + key.size();
      const size_t end = cookies.find(';', start);
      return cookies.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    pos = cookies.find(';', pos);
    if (pos == std::string::npos) break;
  }
  return "";
}

// True when the request carries a valid session or PIN (touching the session).
bool authenticated(httpd_req_t *req) {
  std::lock_guard<std::mutex> lock(g_mutex);
  load();
  if (!g_pin_set) return true;
  std::string token = cookie_value(req);
  if (token.empty()) {
    const std::string bearer = header(req, "Authorization");
    if (bearer.compare(0, 7, "Bearer ") == 0) token = bearer.substr(7);
  }
  if (!token.empty()) {
    if (g_sessions.touch(token.c_str(), esp_timer_get_time())) return true;
  }
  const std::string pin = header(req, "X-P64-Pin");
  if (!pin.empty()) return check_pin(pin);
  return false;
}

esp_err_t refuse(httpd_req_t *req) {
  int locked;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    locked = locked_seconds();
  }
  if (locked > 0) {
    char after[16];  // httpd keeps the pointer until the response goes out: not a temporary
    std::snprintf(after, sizeof(after), "%d", locked);
    httpd_resp_set_hdr(req, "Retry-After", after);
    return reply_error(req, "429 Too Many Requests", "LOCKED", "too many wrong PINs; wait " + std::to_string(locked) + " s");
  }
  return reply_error(req, "401 Unauthorized", "UNAUTHORIZED", "PIN required");
}

cJSON *state_json(bool authenticated_now) {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "pin_set", g_pin_set);
  cJSON_AddBoolToObject(d, "authenticated", authenticated_now);
  cJSON_AddNumberToObject(d, "locked_for_s", locked_seconds());
  return d;
}

esp_err_t auth_get(httpd_req_t *req) {
  const bool ok = authenticated(req);
  std::lock_guard<std::mutex> lock(g_mutex);
  return reply_ok(req, state_json(ok));
}

esp_err_t auth_login(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(body, "pin");
  const std::string pin = p && cJSON_IsString(p) ? p->valuestring : "";
  cJSON_Delete(body);
  std::string cookie;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    load();
    if (g_pin_set && !check_pin(pin)) {
      const int locked = locked_seconds();
      if (locked > 0) {
        char after[16];
        std::snprintf(after, sizeof(after), "%d", locked);
        httpd_resp_set_hdr(req, "Retry-After", after);
        return reply_error(req, "429 Too Many Requests", "LOCKED", "too many wrong PINs; wait " + std::to_string(locked) + " s");
      }
      return reply_error(req, "401 Unauthorized", "WRONG_PIN", "wrong PIN");
    }
    if (g_pin_set) cookie = std::string(kCookie) + "=" + new_session() + "; Path=/; SameSite=Lax; Max-Age=31536000";
  }
  if (!cookie.empty()) httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
  std::lock_guard<std::mutex> lock(g_mutex);
  return reply_ok(req, state_json(true));
}

esp_err_t auth_logout(httpd_req_t *req) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string token = cookie_value(req);
    g_sessions.end(token.c_str());
  }
  const std::string cookie = std::string(kCookie) + "=; Path=/; Max-Age=0";
  httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
  std::lock_guard<std::mutex> lock(g_mutex);
  return reply_ok(req, state_json(false));
}

// Set, change or clear the PIN: {"pin": "1234", "current": "0000"}; "" clears. With a
// PIN set, the request must be authenticated and carry the current PIN.
esp_err_t auth_pin_put(httpd_req_t *req) {
  if (!authenticated(req)) return refuse(req);
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(body, "pin");
  const cJSON *c = cJSON_GetObjectItemCaseSensitive(body, "current");
  const std::string pin = p && cJSON_IsString(p) ? p->valuestring : "";
  const std::string current = c && cJSON_IsString(c) ? c->valuestring : "";
  cJSON_Delete(body);
  if (!pin.empty() && !valid_pin(pin)) return reply_error(req, "400 Bad Request", "INVALID_ARG", "PIN: 4 to 8 digits");
  std::lock_guard<std::mutex> lock(g_mutex);
  load();
  if (g_pin_set && !check_pin(current)) return reply_error(req, "401 Unauthorized", "WRONG_PIN", "current PIN wrong");
  if (!store(pin)) return reply_error(req, "500 Internal Server Error", "NVS", "could not store the PIN");
  g_sessions.end_all();  // every browser signs in again
  ESP_LOGI(TAG, "PIN %s", pin.empty() ? "cleared" : "set");
  return reply_ok(req, state_json(!g_pin_set));
}

}  // namespace

bool gate(httpd_req_t *req) {
  if (authenticated(req)) return true;
  refuse(req);
  return false;
}

void register_routes() {
  const httpd_uri_t routes[] = {
      {"/api/v1/auth", HTTP_GET, auth_get, nullptr, false, false, nullptr},
      {"/api/v1/auth/login", HTTP_POST, auth_login, nullptr, false, false, nullptr},
      {"/api/v1/auth/logout", HTTP_POST, auth_logout, nullptr, false, false, nullptr},
      {"/api/v1/auth/pin", HTTP_PUT, auth_pin_put, nullptr, false, false, nullptr},
  };
  for (const httpd_uri_t &r : routes) net::http::add(r, true);
  std::lock_guard<std::mutex> lock(g_mutex);
  load();
}

}  // namespace auth

void auth_erase() {
  system::on_internal_stack([] {
    nvs_handle_t h;
    if (nvs_open(auth::kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    return true;
  });
  std::lock_guard<std::mutex> lock(auth::g_mutex);
  auth::g_pin_set = false;
  ESP_LOGI(auth::TAG, "PIN erased");
}

}  // namespace p64::web

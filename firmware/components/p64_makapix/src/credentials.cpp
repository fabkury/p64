#include "credentials.hpp"

#include <vector>

#include "esp_log.h"
#include "p64/system/flash_guard.hpp"
#include "nvs.h"

namespace p64::makapix::creds {
namespace {

constexpr const char *TAG = "makapix";
constexpr const char *kNamespace = "makapix";

bool get_str(nvs_handle_t h, const char *key, std::string &out) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return false;
  std::vector<char> buf(len);
  if (nvs_get_str(h, key, buf.data(), &len) != ESP_OK) return false;
  out.assign(buf.data());
  return true;
}

bool get_blob(nvs_handle_t h, const char *key, std::string &out) {
  size_t len = 0;
  if (nvs_get_blob(h, key, nullptr, &len) != ESP_OK || len == 0) return false;
  std::vector<char> buf(len);
  if (nvs_get_blob(h, key, buf.data(), &len) != ESP_OK) return false;
  out.assign(buf.data(), len);
  while (!out.empty() && out.back() == '\0') out.pop_back();
  return true;
}

bool set_blob(nvs_handle_t h, const char *key, const std::string &value) {
  return nvs_set_blob(h, key, value.data(), value.size()) == ESP_OK;
}

}  // namespace

bool load_impl(Credentials &out) {
  out = Credentials{};
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  const bool have_key = get_str(h, "player_key", out.player_key);
  get_blob(h, "ca_pem", out.ca_pem);
  get_blob(h, "cert_pem", out.cert_pem);
  get_blob(h, "key_pem", out.key_pem);
  get_str(h, "api_token", out.api_token);
  get_str(h, "mqtt_host", out.mqtt_host);
  get_str(h, "https_base", out.https_base);
  uint16_t port = 0;
  if (nvs_get_u16(h, "mqtt_port", &port) == ESP_OK) out.mqtt_port = port;
  nvs_close(h);
  return have_key;
}

bool save_impl(const Credentials &c) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "player_key", c.player_key.c_str()) == ESP_OK;
  ok = ok && set_blob(h, "ca_pem", c.ca_pem) && set_blob(h, "cert_pem", c.cert_pem) && set_blob(h, "key_pem", c.key_pem);
  ok = ok && nvs_set_str(h, "api_token", c.api_token.c_str()) == ESP_OK;
  ok = ok && nvs_set_str(h, "mqtt_host", c.mqtt_host.c_str()) == ESP_OK;
  ok = ok && nvs_set_str(h, "https_base", c.https_base.c_str()) == ESP_OK;
  ok = ok && nvs_set_u16(h, "mqtt_port", c.mqtt_port) == ESP_OK;
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  if (!ok) ESP_LOGE(TAG, "storing the credentials failed");
  return ok;
}

bool save_pems_impl(const std::string &ca, const std::string &cert, const std::string &key) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = set_blob(h, "ca_pem", ca) && set_blob(h, "cert_pem", cert) && set_blob(h, "key_pem", key);
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool save_token_impl(const std::string &token) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_str(h, "api_token", token.c_str()) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool erase_impl() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}


// Flash access from any task (flash_guard.hpp): the work runs on an internal stack.
bool load(Credentials &out) { return system::on_internal_stack([&] { return load_impl(out); }); }
bool save(const Credentials &c) { return system::on_internal_stack([&] { return save_impl(c); }); }
bool save_pems(const std::string &ca, const std::string &cert, const std::string &key) { return system::on_internal_stack([&] { return save_pems_impl(ca, cert, key); }); }
bool save_token(const std::string &token) { return system::on_internal_stack([&] { return save_token_impl(token); }); }
bool erase() { return system::on_internal_stack([&] { return erase_impl(); }); }

}  // namespace p64::makapix::creds

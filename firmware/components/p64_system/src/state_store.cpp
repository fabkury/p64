#include "p64/system/state_store.hpp"

#include <vector>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace p64::system::state {
namespace {

constexpr const char *TAG = "state";
constexpr const char *kNamespace = "p64state";

}  // namespace

bool get(const char *key, std::string &out) {
  out.clear();
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 0;
  esp_err_t err = nvs_get_str(h, key, nullptr, &len);
  if (err == ESP_OK && len > 0) {
    std::vector<char> buf(len);
    err = nvs_get_str(h, key, buf.data(), &len);
    if (err == ESP_OK) out.assign(buf.data());
  }
  nvs_close(h);
  return err == ESP_OK;
}

bool set(const char *key, const std::string &value) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(err));
    return false;
  }
  err = nvs_set_str(h, key, value.c_str());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGW(TAG, "set %s: %s", key, esp_err_to_name(err));
  return err == ESP_OK;
}

bool erase(const char *key) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_erase_key(h, key);
  if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool erase_all() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_erase_all(h);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

}  // namespace p64::system::state

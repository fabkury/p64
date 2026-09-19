// p64 -- the PIN (spec 10.3). M9a: only the erase used by the factory reset; the PIN
// itself, the session cookie and the route gate follow.
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "p64/web/web.hpp"

namespace p64::web {

namespace {
constexpr const char *TAG = "auth";
constexpr const char *kNamespace = "p64auth";
}  // namespace

void auth_erase() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);
  ESP_LOGI(TAG, "PIN erased");
}

}  // namespace p64::web

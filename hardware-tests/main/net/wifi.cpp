#include "net/wifi.hpp"

#include <atomic>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace p64::wifi {
namespace {

constexpr const char *TAG = "wifi";
std::atomic<bool> g_connected{false};
int g_failures = 0;

void on_wifi_event(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto *ev = static_cast<wifi_event_sta_disconnected_t *>(data);
    g_connected = false;
    ++g_failures;
    // Back off a little after repeated failures so a wrong password does not hammer the AP.
    const int delay_ms = g_failures < 5 ? 500 : 5000;
    ESP_LOGW(TAG, "disconnected (reason %d), reconnecting in %d ms", ev ? ev->reason : -1, delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto *ev = static_cast<ip_event_got_ip_t *>(data);
    g_failures = 0;
    g_connected = true;
    ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&ev->ip_info.ip));
  }
}

}  // namespace

bool start(const char *ssid, const char *password) {
  if (!ssid || !*ssid) {
    ESP_LOGW(TAG, "no SSID configured (menuconfig > p64 > Wi-Fi and time, or sdkconfig.secrets); running offline");
    return false;
  }
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
    return false;
  }
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, nullptr));

  wifi_config_t cfg = {};
  std::strncpy(reinterpret_cast<char *>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(cfg.sta.password), password ? password : "", sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = (password && *password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "joining \"%s\"", ssid);
  return true;
}

bool connected() { return g_connected.load(); }

}  // namespace p64::wifi

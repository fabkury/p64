#include "net/clock.hpp"

#include <atomic>
#include <cstdlib>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

namespace p64::clock {
namespace {

constexpr const char *TAG = "clock";
std::atomic<bool> g_synced{false};

void on_sync(struct timeval *) {
  if (!g_synced.exchange(true)) {
    int h, m, s;
    if (local_time(h, m, s)) ESP_LOGI(TAG, "time synced: %02d:%02d:%02d local", h, m, s);
  } else {
    ESP_LOGD(TAG, "time re-synced");
  }
}

}  // namespace

void start(const char *tz, const char *ntp_server) {
  setenv("TZ", tz, 1);
  tzset();
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server);
  config.sync_cb = on_sync;
  config.start = true;  // SNTP waits for the network itself
  const esp_err_t err = esp_netif_sntp_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "SNTP started, server %s, TZ %s", ntp_server, tz);
}

bool synced() { return g_synced.load(); }

bool local_time(int &hour, int &minute, int &second) {
  if (!g_synced.load()) return false;
  const time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  hour = t.tm_hour;
  minute = t.tm_min;
  second = t.tm_sec;
  return true;
}

}  // namespace p64::clock

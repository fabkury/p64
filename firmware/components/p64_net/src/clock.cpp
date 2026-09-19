#include "p64/net/clock.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "p64/net/tz.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/rtc.hpp"

namespace p64::net::clock {
namespace {

constexpr const char *TAG = "clock";
std::atomic<bool> g_synced{false};
std::mutex g_mutex;
std::string g_rule = "UTC0";
std::string g_server;
bool g_sntp_started = false;
std::atomic<const char *> g_source{"none"};

void on_sync(struct timeval *) {
  const bool first = !g_synced.exchange(true);
  g_source = "ntp";
  system::rtc::write(time(nullptr));  // the RTC follows NTP (spec 10.2)
  struct tm t;
  if (local_time(t)) {
    ESP_LOGI(TAG, "time %ssynced: %04d-%02d-%02d %02d:%02d:%02d local", first ? "" : "re-", t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
  if (first) system::publish(system::Event::TimeSynced, 1);
}

void apply_zone(const std::string &iana_zone) {
  const char *rule = tz::posix_for(iana_zone);
  if (!rule) {
    ESP_LOGW(TAG, "unknown time zone \"%s\"; using UTC", iana_zone.c_str());
    rule = "UTC0";
  }
  g_rule = rule;
  setenv("TZ", rule, 1);
  tzset();
  ESP_LOGI(TAG, "time zone %s (%s)", iana_zone.c_str(), rule);
}

}  // namespace

void start(const std::string &ntp_server, const std::string &iana_zone) {
  std::lock_guard<std::mutex> lock(g_mutex);
  apply_zone(iana_zone);
  time_t from_rtc;
  if (system::rtc::read(from_rtc)) {
    timeval tv = {from_rtc, 0};
    settimeofday(&tv, nullptr);
    g_synced = true;
    g_source = "rtc";
    struct tm t;
    localtime_r(&from_rtc, &t);
    ESP_LOGI(TAG, "time from the RTC: %04d-%02d-%02d %02d:%02d:%02d local", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    system::publish(system::Event::TimeSynced, 0);
  } else {
    ESP_LOGI(TAG, "no time from the RTC; waiting for NTP");
  }
  g_server = ntp_server;
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(g_server.c_str());
  config.sync_cb = on_sync;
  config.start = true;  // SNTP waits for the network itself
  const esp_err_t err = esp_netif_sntp_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(err));
    return;
  }
  g_sntp_started = true;
  ESP_LOGI(TAG, "SNTP started, server %s", g_server.c_str());
}

void set_timezone(const std::string &iana_zone) {
  std::lock_guard<std::mutex> lock(g_mutex);
  apply_zone(iana_zone);
}

void set_ntp_server(const std::string &server) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (server == g_server) return;
  g_server = server;
  if (g_sntp_started) {
    esp_netif_sntp_deinit();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(g_server.c_str());
    config.sync_cb = on_sync;
    config.start = true;
    if (esp_netif_sntp_init(&config) == ESP_OK) ESP_LOGI(TAG, "SNTP server now %s", g_server.c_str());
  }
}

void set_manual(time_t utc) {
  timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
  const bool first = !g_synced.exchange(true);
  g_source = "manual";
  system::rtc::write(utc);
  ESP_LOGI(TAG, "time set by hand");
  if (first) system::publish(system::Event::TimeSynced, 0);
}

bool synced() { return g_synced.load(); }

const char *source() { return g_source.load(); }

bool local_time(struct tm &out) {
  if (!g_synced.load()) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}

std::string posix_rule() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_rule;
}

}  // namespace p64::net::clock

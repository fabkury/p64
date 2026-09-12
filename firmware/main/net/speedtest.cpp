#include "net/speedtest.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "net/clock.hpp"
#include "net/makapix.hpp"
#include "net/wifi.hpp"

namespace p64::speedtest {
namespace {

constexpr const char *TAG = "speedtest";
constexpr uint32_t kStartAfterMs = 45 * 1000;

struct Result {
  bool ok = false;
  int status = 0;
  size_t bytes = 0;
  int64_t connect_us = 0;  // open + headers: DNS, TCP, TLS, request, first response bytes
  int64_t body_us = 0;     // reading the body
};

// Downloads `url` through `client` (opened with its own config), discarding the data.
Result download(esp_http_client_handle_t client, const char *url) {
  Result r;
  esp_http_client_set_url(client, url);
  const int64_t t0 = esp_timer_get_time();
  const esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
    return r;
  }
  esp_http_client_fetch_headers(client);
  r.status = esp_http_client_get_status_code(client);
  const int64_t t1 = esp_timer_get_time();
  static uint8_t chunk[4096];
  while (true) {
    const int n = esp_http_client_read(client, reinterpret_cast<char *>(chunk), sizeof(chunk));
    if (n < 0) {
      ESP_LOGW(TAG, "read failed after %u bytes", static_cast<unsigned>(r.bytes));
      return r;
    }
    if (n == 0) break;
    r.bytes += static_cast<size_t>(n);
  }
  const int64_t t2 = esp_timer_get_time();
  r.connect_us = t1 - t0;
  r.body_us = t2 - t1;
  r.ok = (r.status == 200);
  return r;
}

void report(const char *name, const Result &r) {
  if (!r.ok) {
    ESP_LOGW(TAG, "%-38s FAILED (status %d, %u bytes)", name, r.status, static_cast<unsigned>(r.bytes));
    return;
  }
  const double body_s = static_cast<double>(r.body_us) / 1e6;
  const double total_s = static_cast<double>(r.connect_us + r.body_us) / 1e6;
  const double body_kbps = body_s > 0 ? r.bytes / 1024.0 / body_s : 0;
  const double total_kbps = total_s > 0 ? r.bytes / 1024.0 / total_s : 0;
  ESP_LOGI(TAG, "%-38s %8u bytes  connect %5lld ms  body %6lld ms  body %7.1f KB/s (%5.2f Mbit/s)  overall %7.1f KB/s",
           name, static_cast<unsigned>(r.bytes), static_cast<long long>(r.connect_us / 1000),
           static_cast<long long>(r.body_us / 1000), body_kbps, body_kbps * 8 / 1000, total_kbps);
}

esp_http_client_handle_t make_client(const char *url, bool keep_alive) {
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 30000;
  cfg.user_agent = "p64-speedtest";
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;
  cfg.keep_alive_enable = keep_alive;
  return esp_http_client_init(&cfg);
}

// One download on a fresh connection.
void single(const char *name, const char *url) {
  esp_http_client_handle_t client = make_client(url, false);
  if (!client) return;
  report(name, download(client, url));
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}

void log_link() {
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    ESP_LOGI(TAG, "link: rssi %d dBm, channel %d%s, 11n %s, free heap %u (internal %u)", ap.rssi, ap.primary,
             ap.second == WIFI_SECOND_CHAN_NONE ? "" : " (40 MHz)", ap.phy_11n ? "yes" : "no",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  }
}

[[maybe_unused]] void task(void *) {  // only referenced when P64_SPEEDTEST is set
  vTaskDelay(pdMS_TO_TICKS(kStartAfterMs));
  while (!(wifi::connected() && clock::synced())) vTaskDelay(pdMS_TO_TICKS(500));
  // Keep the link to ourselves: hold the artwork fetcher and let an in-flight fetch end.
  makapix::set_paused(true);
  while (makapix::busy()) vTaskDelay(pdMS_TO_TICKS(250));
  ESP_LOGI(TAG, "starting; the show keeps running meanwhile, the artwork fetcher is held");
  log_link();

  // Elsewhere on the Internet: a CDN endpoint that serves any size over HTTPS, and a
  // plain-HTTP mirror to show the cost of TLS. Twice each: the link varies.
  for (int i = 0; i < 2; ++i) {
    single("cloudflare https 1 MB", "https://speed.cloudflare.com/__down?bytes=1000000");
  }
  single("cloudflare https 5 MB", "https://speed.cloudflare.com/__down?bytes=5000000");
  single("thinkbroadband http 5 MB", "http://ipv4.download.thinkbroadband.com/5MB.zip");

  // Makapix Club: one artwork on a cold connection (twice), then the five largest
  // promoted GIFs back to back on one kept-alive connection (how a player would batch).
  for (int i = 0; i < 2; ++i) {
    single("makapix https cold, 1 GIF", "https://makapix.club/api/d/juBK.gif");
  }
  {
    static const char *const sqids[] = {"juBK", "peE", "Nhm", "zaG", "err"};
    esp_http_client_handle_t client = make_client("https://makapix.club/api/d/juBK.gif", true);
    if (client) {
      size_t total = 0;
      int64_t total_us = 0;
      for (const char *sqid : sqids) {
        char url[96], name[48];
        std::snprintf(url, sizeof(url), "https://makapix.club/api/d/%s.gif", sqid);
        std::snprintf(name, sizeof(name), "makapix keep-alive %s", sqid);
        const Result r = download(client, url);
        report(name, r);
        if (r.ok) {
          total += r.bytes;
          total_us += r.connect_us + r.body_us;
        }
      }
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      if (total_us > 0) {
        const double kbps = total / 1024.0 / (static_cast<double>(total_us) / 1e6);
        ESP_LOGI(TAG, "makapix keep-alive total: %u bytes in %lld ms = %.1f KB/s (%.2f Mbit/s)",
                 static_cast<unsigned>(total), static_cast<long long>(total_us / 1000), kbps, kbps * 8 / 1000);
      }
    }
  }
  log_link();
  makapix::set_paused(false);
  ESP_LOGI(TAG, "done");
  vTaskDelete(nullptr);
}

}  // namespace

void start() {
#if defined(CONFIG_P64_SPEEDTEST)
  xTaskCreatePinnedToCore(task, "speedtest", 12 * 1024, nullptr, 3, nullptr, 0);
  ESP_LOGI(TAG, "scheduled %lu s after boot", static_cast<unsigned long>(kStartAfterMs / 1000));
#endif
}

}  // namespace p64::speedtest

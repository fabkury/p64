#include "p64/net/wifi.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "p64/net/dns_hijack.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/flash_guard.hpp"
#include "sdkconfig.h"

// Locking discipline: g_mutex guards the state fields only. No Wi-Fi driver call is made
// while it is held, because the driver delivers events on the event-loop task, whose
// handler needs the same mutex (holding it across esp_wifi_set_mode() deadlocked the
// device on 2026-09-19).

namespace p64::net::wifi {
namespace {

constexpr const char *TAG = "wifi";
constexpr const char *kNamespace = "wifi";
constexpr const char *kApSsid = "p64-setup";
constexpr int64_t kFallbackUs = 60LL * 1000 * 1000;    // without a connection, open setup mode
constexpr int64_t kSetupRetryUs = 30LL * 1000 * 1000;  // retry the saved network while in setup mode
constexpr int kMaxBackoffS = 30;

std::mutex g_mutex;
esp_netif_t *g_sta = nullptr;
esp_netif_t *g_ap = nullptr;
bool g_started = false;
bool g_connected = false;
bool g_setup_mode = false;
bool g_have_credentials = false;
std::string g_ssid, g_password, g_hostname = "p64";
std::string g_ip, g_gateway, g_netmask;
int g_backoff_s = 1;
esp_timer_handle_t g_fallback_timer = nullptr;
esp_timer_handle_t g_reconnect_timer = nullptr;

// --- NVS ----------------------------------------------------------------------

bool nvs_load_impl(std::string &ssid, std::string &password) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  char s[33] = {0}, p[65] = {0};
  size_t n = sizeof(s);
  const bool ok = nvs_get_str(h, "ssid", s, &n) == ESP_OK && s[0] != 0;
  n = sizeof(p);
  nvs_get_str(h, "password", p, &n);
  nvs_close(h);
  if (!ok) return false;
  ssid = s;
  password = p;
  return true;
}

bool nvs_store_impl(const std::string &ssid, const std::string &password) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(h, "ssid", ssid.c_str());
  if (err == ESP_OK) err = nvs_set_str(h, "password", password.c_str());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool nvs_erase_impl() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  nvs_erase_all(h);
  const esp_err_t err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

// Every NVS access through the flash guard (architecture section 20): the callers today
// all run on internal stacks, but nothing else enforced it (review of 2026-09-22).
bool nvs_load(std::string &ssid, std::string &password) {
  return system::on_internal_stack([&] { return nvs_load_impl(ssid, password); });
}
bool nvs_store(const std::string &ssid, const std::string &password) {
  return system::on_internal_stack([&] { return nvs_store_impl(ssid, password); });
}
bool nvs_erase() { return system::on_internal_stack([] { return nvs_erase_impl(); }); }

// --- driver helpers (never called with g_mutex held) --------------------------

void apply_sta_config(const std::string &ssid, const std::string &password) {
  wifi_config_t cfg = {};
  std::strncpy(reinterpret_cast<char *>(cfg.sta.ssid), ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(cfg.sta.password), password.c_str(), sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;
  esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

void start_mdns(const std::string &hostname) {
  static bool inited = false;
  if (!inited) {
    if (mdns_init() != ESP_OK) {
      ESP_LOGW(TAG, "mDNS init failed");
      return;
    }
    inited = true;
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  }
  mdns_hostname_set(hostname.c_str());
  mdns_instance_name_set("p64");
}

void enter_setup_mode() {
  bool retry = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_setup_mode) return;
    g_setup_mode = true;
    retry = g_have_credentials;
  }
  ESP_LOGW(TAG, "opening setup mode: access point \"%s\" (open), portal at 192.168.4.1", kApSsid);
  wifi_config_t ap = {};
  std::strncpy(reinterpret_cast<char *>(ap.ap.ssid), kApSsid, sizeof(ap.ap.ssid) - 1);
  ap.ap.ssid_len = std::strlen(kApSsid);
  ap.ap.channel = 1;
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  ESP_LOGI(TAG, "setup mode: set_mode(APSTA) -> %s", esp_err_to_name(err));
  err = esp_wifi_set_config(WIFI_IF_AP, &ap);
  ESP_LOGI(TAG, "setup mode: AP config -> %s", esp_err_to_name(err));
  dns_hijack::start();
  system::publish(system::Event::SetupModeStarted);
  if (retry) {
    esp_timer_stop(g_reconnect_timer);
    esp_timer_start_periodic(g_reconnect_timer, kSetupRetryUs);
  }
}

void leave_setup_mode() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_setup_mode) return;
    g_setup_mode = false;
  }
  ESP_LOGI(TAG, "closing setup mode");
  esp_timer_stop(g_reconnect_timer);
  dns_hijack::stop();
  esp_wifi_set_mode(WIFI_MODE_STA);
  system::publish(system::Event::SetupModeStopped);
}

void on_fallback_timer(void *) {
  bool connected;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    connected = g_connected;
  }
  if (!connected) enter_setup_mode();
}

void on_reconnect_timer(void *) {
  bool go;
  std::string ssid;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    go = !g_connected && g_have_credentials;
    ssid = g_ssid;
  }
  if (go) {
    ESP_LOGI(TAG, "retrying \"%s\"", ssid.c_str());
    esp_wifi_connect();
  }
}

void on_event(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    bool go;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      go = g_have_credentials;
    }
    if (go) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto *ev = static_cast<wifi_event_sta_disconnected_t *>(data);
    bool was_connected, retry_now = false, arm_fallback = false;
    int backoff_s = 1;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      was_connected = g_connected;
      g_connected = false;
      g_ip.clear();
      if (was_connected) arm_fallback = true;
      if (!g_setup_mode && g_have_credentials) {
        retry_now = true;
        backoff_s = g_backoff_s;
        g_backoff_s = std::min(g_backoff_s * 2, kMaxBackoffS);
      }
    }
    if (was_connected) {
      ESP_LOGW(TAG, "disconnected (reason %d)", ev ? ev->reason : -1);
      system::publish(system::Event::WifiDisconnected);
    } else {
      ESP_LOGD(TAG, "connect failed (reason %d)", ev ? ev->reason : -1);
    }
    if (arm_fallback) {
      esp_timer_stop(g_fallback_timer);
      esp_timer_start_once(g_fallback_timer, kFallbackUs);
    }
    if (retry_now) {
      // Exponential backoff between attempts; the fallback timer decides setup mode.
      esp_timer_stop(g_reconnect_timer);
      esp_timer_start_once(g_reconnect_timer, static_cast<int64_t>(backoff_s) * 1000 * 1000);
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto *ev = static_cast<ip_event_got_ip_t *>(data);
    char buf[16];
    std::string ssid, hostname, ip;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_connected = true;
      g_backoff_s = 1;
      std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.ip));
      g_ip = buf;
      std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.gw));
      g_gateway = buf;
      std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.netmask));
      g_netmask = buf;
      ssid = g_ssid;
      hostname = g_hostname;
      ip = g_ip;
    }
    ESP_LOGI(TAG, "connected to \"%s\", ip %s, hostname %s.local", ssid.c_str(), ip.c_str(), hostname.c_str());
    esp_timer_stop(g_fallback_timer);
    esp_timer_stop(g_reconnect_timer);
    leave_setup_mode();
    start_mdns(hostname);
    system::publish(system::Event::WifiConnected);
  }
}

}  // namespace

bool start(const std::string &hostname) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_started) return true;
    g_hostname = hostname;
  }
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  g_sta = esp_netif_create_default_wifi_sta();
  g_ap = esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(g_sta, hostname.c_str());
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, nullptr));
  esp_wifi_set_ps(WIFI_PS_NONE);  // latency for streams and the web UI over power savings

  const esp_timer_create_args_t fb = {.callback = on_fallback_timer, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                      .name = "wifi_fallback", .skip_unhandled_events = true};
  esp_timer_create(&fb, &g_fallback_timer);
  const esp_timer_create_args_t rc = {.callback = on_reconnect_timer, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                      .name = "wifi_retry", .skip_unhandled_events = true};
  esp_timer_create(&rc, &g_reconnect_timer);

  std::string ssid, password;
  bool have = nvs_load(ssid, password);
#if defined(CONFIG_P64_DEV_WIFI_SSID)
  if (!have && CONFIG_P64_DEV_WIFI_SSID[0] != 0) {
    if (nvs_store(CONFIG_P64_DEV_WIFI_SSID, CONFIG_P64_DEV_WIFI_PASSWORD)) {
      ESP_LOGW(TAG, "no stored credentials: seeded from the development build configuration");
      have = nvs_load(ssid, password);
    }
  }
#endif
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_have_credentials = have;
    g_ssid = ssid;
    g_password = password;
    g_started = true;
  }
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  if (have) {
    apply_sta_config(ssid, password);
    ESP_LOGI(TAG, "joining \"%s\"", ssid.c_str());
  }
  ESP_ERROR_CHECK(esp_wifi_start());
  if (have) {
    esp_timer_start_once(g_fallback_timer, kFallbackUs);
  } else {
    ESP_LOGW(TAG, "no Wi-Fi credentials");
    enter_setup_mode();
  }
  return true;
}

bool has_credentials() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_have_credentials;
}

std::string saved_ssid() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ssid;
}

bool save_credentials(const std::string &ssid, const std::string &password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return false;
  if (!nvs_store(ssid, password)) return false;
  bool in_setup;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ssid = ssid;
    g_password = password;
    g_have_credentials = true;
    g_backoff_s = 1;
    in_setup = g_setup_mode;
  }
  apply_sta_config(ssid, password);
  ESP_LOGI(TAG, "credentials saved; joining \"%s\"", ssid.c_str());
  esp_wifi_disconnect();
  esp_wifi_connect();
  esp_timer_stop(g_fallback_timer);
  esp_timer_start_once(g_fallback_timer, kFallbackUs);
  if (in_setup) {
    esp_timer_stop(g_reconnect_timer);
    esp_timer_start_periodic(g_reconnect_timer, kSetupRetryUs);
  }
  return true;
}

bool erase_credentials() {
  ESP_LOGI(TAG, "erasing credentials");
  if (!nvs_erase()) return false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ssid.clear();
    g_password.clear();
    g_have_credentials = false;
    g_connected = false;
    g_ip.clear();
  }
  esp_timer_stop(g_fallback_timer);
  esp_timer_stop(g_reconnect_timer);
  const esp_err_t err = esp_wifi_disconnect();
  ESP_LOGI(TAG, "erase: disconnect -> %s", esp_err_to_name(err));
  enter_setup_mode();
  ESP_LOGI(TAG, "erase: done");
  return true;
}

Status status() {
  Status s;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    s.connected = g_connected;
    s.setup_mode = g_setup_mode;
    s.ssid = g_ssid;
    s.ip = g_ip;
    s.gateway = g_gateway;
    s.netmask = g_netmask;
    s.hostname = g_hostname;
  }
  if (s.setup_mode) {
    s.ap_ssid = kApSsid;
    s.ap_ip = "192.168.4.1";
  }
  if (s.connected) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s.rssi = ap.rssi;
  }
  return s;
}

std::vector<ScanEntry> scan() {
  std::vector<ScanEntry> out;
  wifi_scan_config_t cfg = {};
  cfg.show_hidden = false;
  if (esp_wifi_scan_start(&cfg, true) != ESP_OK) return out;
  uint16_t count = 0;
  esp_wifi_scan_get_ap_num(&count);
  if (count == 0) return out;
  std::vector<wifi_ap_record_t> records(count);
  if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) return out;
  for (uint16_t i = 0; i < count; ++i) {
    const std::string ssid(reinterpret_cast<const char *>(records[i].ssid));
    if (ssid.empty()) continue;
    auto it = std::find_if(out.begin(), out.end(), [&](const ScanEntry &e) { return e.ssid == ssid; });
    if (it != out.end()) {
      it->rssi = std::max(it->rssi, static_cast<int>(records[i].rssi));
      continue;
    }
    out.push_back(ScanEntry{ssid, records[i].rssi, records[i].authmode != WIFI_AUTH_OPEN});
  }
  std::sort(out.begin(), out.end(), [](const ScanEntry &a, const ScanEntry &b) { return a.rssi > b.rssi; });
  return out;
}

void set_hostname(const std::string &hostname) {
  bool connected;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hostname = hostname;
    connected = g_connected;
  }
  if (g_sta) esp_netif_set_hostname(g_sta, hostname.c_str());
  if (connected) start_mdns(hostname);
}

}  // namespace p64::net::wifi

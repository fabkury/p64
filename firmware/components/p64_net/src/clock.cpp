#include "p64/net/clock.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <sys/time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "lwip/ip_addr.h"
#include "p64/net/time_rules.hpp"
#include "p64/net/tz.hpp"
#include "p64/net/wifi.hpp"
#include "p64/system/event_bus.hpp"
#include "sdkconfig.h"

// The SNTP configuration ADR 0011 relies on lives in sdkconfig.defaults; a generated
// sdkconfig older than it fails here instead of running without it (delete
// firmware/sdkconfig and build again).
#ifndef CONFIG_LWIP_DHCP_GET_NTP_SRV
#error "CONFIG_LWIP_DHCP_GET_NTP_SRV is off: delete firmware/sdkconfig so sdkconfig.defaults applies"
#endif
static_assert(CONFIG_LWIP_SNTP_MAX_SERVERS >= p64::net::time_rules::kSlots,
              "CONFIG_LWIP_SNTP_MAX_SERVERS is too small: delete firmware/sdkconfig so sdkconfig.defaults applies");
static_assert(CONFIG_LWIP_DHCP_MAX_NTP_SERVERS == 1, "the router's server takes slot 0 only (time_rules::plan)");
static_assert(CONFIG_LWIP_SNTP_UPDATE_DELAY == 6 * 3600 * 1000,
              "the re-sync interval is 6 h (ADR 0011): delete firmware/sdkconfig so sdkconfig.defaults applies");

namespace p64::net::clock {
namespace {

constexpr const char *TAG = "clock";
constexpr int64_t kSecondUs = 1000 * 1000;
constexpr uint64_t kTickUs = 30ULL * 1000 * 1000;
constexpr int64_t kRestartAfterRejectUs = 5LL * 60 * kSecondUs;
constexpr int64_t kStaleWarnUs = 24LL * 3600 * kSecondUs;

// Guards the zone rule, the server names and every change to the SNTP slots.
std::mutex g_mutex;
std::string g_rule = "UTC0";
// lwIP keeps the pointer it is given, not a copy: the setting lives in one of two
// buffers, and a change writes the other one, repoints the slots, and only then may the
// old buffer be reused (at the change after next).
std::string g_server_buf[2];
int g_server_cur = 0;
bool g_started = false;
esp_timer_handle_t g_tick = nullptr;

// Written before SNTP starts, read on the lwIP thread afterwards.
int64_t g_ntp_floor = time_rules::ntp_floor(time_rules::kEarliestBuildDate);
int64_t g_file_floor = time_rules::file_floor(time_rules::kEarliestBuildDate);

std::atomic<bool> g_synced{false};
std::atomic<int64_t> g_last_sync_us{0};
std::atomic<uint32_t> g_syncs{0};
std::atomic<uint32_t> g_rejected{0};
std::atomic<bool> g_reject_pending{false};
std::atomic<int64_t> g_connected_us{-1};  // -1 while Wi-Fi is down
// Tick-only state (the esp_timer task).
int64_t g_last_restart_us = 0;
std::atomic<int64_t> g_wait_warned_at_s{-1};  // reset by on_connected (bus task)
int64_t g_stale_warned_us = 0;

const std::string &setting() { return g_server_buf[g_server_cur]; }

time_rules::Slots read_slots() {
  time_rules::Slots s;
  for (size_t i = 0; i < time_rules::kSlots; ++i) {
    const char *name = esp_sntp_getservername(static_cast<u8_t>(i));
    s[i].name = name ? name : "";
    const ip_addr_t *addr = esp_sntp_getserver(static_cast<u8_t>(i));
    s[i].addressed = addr && !ip_addr_isany(addr);
  }
  return s;
}

const char *name_for(time_rules::Want want) {
  switch (want) {
    case time_rules::Want::Setting: return setting().c_str();
    case time_rules::Want::Fallback0: return time_rules::kFallbacks[0];
    case time_rules::Want::Fallback1: return time_rules::kFallbacks[1];
    default: return nullptr;
  }
}

// Brings the slots to the plan; `keep_dhcp` false forgets the router's server (after a
// disconnect: the next network may offer none). Caller holds g_mutex.
void apply_slots_locked(bool keep_dhcp, const char *why) {
  const time_rules::Slots actual = read_slots();
  const std::vector<time_rules::Repair> fix = time_rules::repairs(actual, setting(), keep_dhcp);
  for (const time_rules::Repair &r : fix) {
    const u8_t idx = static_cast<u8_t>(r.slot);
    if (r.want == time_rules::Want::Empty) {
      esp_sntp_setserver(idx, nullptr);
    } else {
      esp_sntp_setservername(idx, name_for(r.want));
    }
  }
  if (!fix.empty()) {
    const bool dhcp = keep_dhcp && time_rules::from_dhcp(actual[0]);
    char dhcp_addr[IPADDR_STRLEN_MAX] = "";
    if (dhcp) ipaddr_ntoa_r(esp_sntp_getserver(0), dhcp_addr, sizeof(dhcp_addr));
    ESP_LOGI(TAG, "NTP servers (%s): %s%s%s, then %s, %s", why, dhcp ? dhcp_addr : "", dhcp ? " (router), " : "",
             setting().c_str(), time_rules::kFallbacks[0], time_rules::kFallbacks[1]);
  }
}

void on_sync();

// An answer from lwIP (see sntp_sync_time below).
void on_answer(struct timeval *tv) {
  if (!time_rules::accept_ntp(tv->tv_sec, g_ntp_floor)) {
    ++g_rejected;
    g_reject_pending = true;  // the tick asks again (lwIP would wait a full interval)
    ESP_LOGW(TAG, "NTP answer refused: %lld is earlier than the firmware's build date; the clock is unchanged",
             static_cast<long long>(tv->tv_sec));
    return;
  }
  settimeofday(tv, nullptr);
  sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
  on_sync();
}

void on_sync() {
  const bool first = !g_synced.exchange(true);
  g_last_sync_us = esp_timer_get_time();
  ++g_syncs;
  struct tm t;
  if (local_time(t)) {
    ESP_LOGI(TAG, "%s: %04d-%02d-%02d %02d:%02d:%02d local", first ? "time synced from NTP" : "NTP re-sync",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
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

void on_connected(const system::Message &) {
  g_connected_us = esp_timer_get_time();
  g_wait_warned_at_s = -1;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_started) return;
  // DHCP has run by now: a router's server sits in slot 0 and slots 1..3 were cleared.
  apply_slots_locked(true, "connected");
  // Ask now rather than when a retry timer from before the connection runs out.
  if (!g_synced.load()) esp_sntp_restart();
}

void on_disconnected(const system::Message &) {
  g_connected_us = -1;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_started) apply_slots_locked(false, "disconnected");
}

void tick(void *) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // A DHCP lease renewal that carries an NTP server clears slots 1..3 without any
    // event (the address does not change): put the setting and the fallbacks back.
    if (g_started) apply_slots_locked(true, "restored after a DHCP renewal");
  }
  const int64_t now = esp_timer_get_time();
  if (!g_synced.load()) {
    if (g_reject_pending.load() && now - g_last_restart_us >= kRestartAfterRejectUs) {
      g_reject_pending = false;
      g_last_restart_us = now;
      esp_sntp_restart();
    }
    const int64_t connected_us = g_connected_us.load();
    if (connected_us >= 0) {
      const int64_t connected_s = (now - connected_us) / kSecondUs;
      if (time_rules::wait_warning_due(connected_s, g_wait_warned_at_s.load())) {
        g_wait_warned_at_s = connected_s;
        ESP_LOGW(TAG,
                 "no NTP answer %lld s after Wi-Fi connected: clocks, Makapix, the night schedule and the cache "
                 "sweep wait for the time (is UDP port 123 blocked on this network?)",
                 static_cast<long long>(connected_s));
      }
    }
    return;
  }
  const int64_t since = now - g_last_sync_us.load();
  if (since > kStaleWarnUs && now - g_stale_warned_us > kStaleWarnUs) {
    g_stale_warned_us = now;
    ESP_LOGW(TAG, "NTP has not answered for %lld h; the time stays trusted until the next reboot",
             static_cast<long long>(since / (3600 * kSecondUs)));
  }
}

}  // namespace

void start(const std::string &ntp_server, const std::string &iana_zone) {
  const esp_app_desc_t *app = esp_app_get_description();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_started) return;
    apply_zone(iana_zone);
    int64_t build = 0;
    if (!time_rules::parse_build_date(app->date, build)) {
      ESP_LOGW(TAG, "build date \"%s\" not understood; the time floor is 2026-01-01", app->date);
      build = time_rules::kEarliestBuildDate;
    }
    g_ntp_floor = time_rules::ntp_floor(build);
    g_file_floor = time_rules::file_floor(build);
    g_server_buf[g_server_cur] = ntp_server;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_servermode_dhcp(true);
    apply_slots_locked(false, "boot");
    esp_sntp_init();
    g_started = true;
  }
  system::subscribe(system::Event::WifiConnected, on_connected);
  system::subscribe(system::Event::WifiDisconnected, on_disconnected);
  const esp_timer_create_args_t args = {tick, nullptr, ESP_TIMER_TASK, "clock", false};
  if (esp_timer_create(&args, &g_tick) == ESP_OK) esp_timer_start_periodic(g_tick, kTickUs);
  ESP_LOGI(TAG, "SNTP started, every %lu s; the time is trusted once NTP answers (not before %s)",
           static_cast<unsigned long>(sntp_get_sync_interval() / 1000), app->date);
  // The connection may have come up before the subscription.
  if (wifi::status().connected) on_connected(system::Message{system::Event::WifiConnected, 0});
}

void set_timezone(const std::string &iana_zone) {
  std::lock_guard<std::mutex> lock(g_mutex);
  apply_zone(iana_zone);
}

void set_ntp_server(const std::string &server) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (server == setting()) return;
  g_server_cur ^= 1;
  g_server_buf[g_server_cur] = server;
  if (!g_started) return;
  apply_slots_locked(true, "setting changed");
  esp_sntp_restart();  // ask the new server now
}

bool synced() { return g_synced.load(); }

const char *source() { return g_synced.load() ? "ntp" : "none"; }

bool now_utc(time_t &out) {
  if (!g_synced.load()) return false;
  out = time(nullptr);
  return true;
}

bool local_time(struct tm &out) {
  time_t now;
  if (!now_utc(now)) return false;
  localtime_r(&now, &out);
  return true;
}

std::string posix_rule() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_rule;
}

int64_t file_date_floor() { return g_file_floor; }

Status status() {
  Status s;
  const int64_t now = esp_timer_get_time();
  s.synced = g_synced.load();
  if (s.synced) s.last_sync_s = (now - g_last_sync_us.load()) / kSecondUs;
  s.syncs = g_syncs.load();
  s.rejected = g_rejected.load();
  const int64_t connected_us = g_connected_us.load();
  if (!s.synced && connected_us >= 0) s.waiting_s = (now - connected_us) / kSecondUs;
  s.interval_s = sntp_get_sync_interval() / 1000;
  std::lock_guard<std::mutex> lock(g_mutex);
  const time_rules::Slots slots = read_slots();
  for (size_t i = 0; i < time_rules::kSlots; ++i) {
    Server server;
    if (time_rules::from_dhcp(slots[i])) {
      char buf[IPADDR_STRLEN_MAX] = "";
      ipaddr_ntoa_r(esp_sntp_getserver(static_cast<u8_t>(i)), buf, sizeof(buf));
      server.host = buf;
      server.dhcp = true;
    } else if (!slots[i].name.empty()) {
      server.host = slots[i].name;
    } else {
      continue;
    }
    server.reach = esp_sntp_getreachability(static_cast<u8_t>(i));
    s.servers.push_back(std::move(server));
  }
  return s;
}

}  // namespace p64::net::clock

// ESP-IDF's weak hook between lwIP's SNTP client and the system clock, replaced so an
// answer earlier than the firmware's build date never reaches the clock (ADR 0011). It
// runs on the lwIP thread: no SNTP API calls from here (they post to that same thread).
extern "C" void sntp_sync_time(struct timeval *tv) { p64::net::clock::on_answer(tv); }

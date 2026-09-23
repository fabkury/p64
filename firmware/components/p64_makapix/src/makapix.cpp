#include "p64/makapix/makapix.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <ctime>

#include "cJSON.h"
#include "cache.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "contract.hpp"
#include "internal.hpp"
#include "mbedtls/x509_crt.h"
#include "mqtt.hpp"
#include "p64/content/playset_json.hpp"
#include "p64/content/psram.hpp"
#include "p64/net/clock.hpp"
#include "p64/net/wifi.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"
#include "sdkconfig.h"

namespace p64::makapix {

// --- shared state (internal.hpp) ------------------------------------------------------

namespace internal {

std::mutex g_mutex;
Status g_status;
creds::Credentials g_creds;
Hooks g_hooks;
std::vector<std::unique_ptr<Channel>> g_channels;
int64_t g_next_poll_us = 0;

Channel *find_channel(const std::string &id) {
  for (auto &ch : g_channels) {
    if (ch->id == id) return ch.get();
  }
  return nullptr;
}

Channel &ensure_channel(const ChannelRef &ref) {
  const std::string id = channel_id(ref);
  if (Channel *ch = find_channel(id)) return *ch;
  auto ch = std::make_unique<Channel>();
  ch->ref = ref;
  ch->id = id;
  g_channels.push_back(std::move(ch));
  return *g_channels.back();
}

void recount_cached(Channel &ch) {
  uint32_t n = 0;
  for (const content::MakapixEntry &e : ch.entries) {
    if (e.flags & content::kMakapixCached) ++n;
  }
  ch.cached = n;
}

void set_state(State state) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_status.state == state) return;
    g_status.state = state;
  }
  system::publish(system::Event::MakapixStateChanged, static_cast<int32_t>(state));
}

void set_error(const std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.last_error = error;
}

void set_activity(const std::string &activity) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.activity = activity;
}

void publish_channel_changed() { system::publish(system::Event::MakapixChannelChanged); }

uint16_t g_max_side = 0;  // the size limit the channel indexes were walked with

// A changed size limit walks every channel again so the indexes hold only what plays
// (the show skips oversized entries meanwhile, and in indexes older than the change).
void on_settings_changed() {
  const uint16_t side = system::settings().makapix_max_side;
  bool changed;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    changed = side != g_max_side;
    g_max_side = side;
    if (changed) {
      for (auto &ch : g_channels) {
        if (ch->refreshing) {
          ch->rewalk = true;
        } else {
          ch->next_refresh_us = 0;
          ch->retry_at_us = 0;
        }
      }
    }
  }
  if (changed) {
    ESP_LOGI(TAG, "size limit %ux%u: every channel refreshes", side, side);
    publish_channel_changed();
  }
}

bool online() { return net::wifi::status().connected && net::clock::synced(); }

uint32_t epoch_now() {
  time_t t;
  return net::clock::now_utc(t) && t > 0 ? static_cast<uint32_t>(t) : 0;
}

std::string iso_now() {
  time_t t;
  if (!net::clock::now_utc(t)) return "";
  struct tm utc;
  gmtime_r(&t, &utc);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}

}  // namespace internal

using namespace internal;

namespace {

constexpr int64_t kViewAfterUs = 5 * 1000000;  // an artwork counts as viewed after 5 s on the panel
constexpr int64_t kStatusThrottleUs = 5 * 1000000;

// What is on the panel, for views and presence.
int32_t g_current_post = -1;
ChannelRef g_current_channel;
bool g_current_has_channel = false;
bool g_current_intentional = false;
int64_t g_last_status_us = 0;
esp_timer_handle_t g_view_timer = nullptr;

}  // namespace
namespace internal {
uint32_t cert_not_after(const std::string &pem) {
  if (pem.empty()) return 0;
  mbedtls_x509_crt crt;
  mbedtls_x509_crt_init(&crt);
  const int rc = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char *>(pem.c_str()), pem.size() + 1);
  uint32_t out = 0;
  if (rc == 0) {
    struct tm t = {};
    t.tm_year = crt.valid_to.year - 1900;
    t.tm_mon = crt.valid_to.mon - 1;
    t.tm_mday = crt.valid_to.day;
    t.tm_hour = crt.valid_to.hour;
    t.tm_min = crt.valid_to.min;
    t.tm_sec = crt.valid_to.sec;
    // timegm is not in newlib: compute the epoch of a UTC tm by hand.
    const int y = t.tm_year + 1900;
    const int m = t.tm_mon + 1;
    const int64_t yy = m <= 2 ? y - 1 : y;
    const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097LL + static_cast<int64_t>(doe) - 719468;
    out = static_cast<uint32_t>(days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
  }
  mbedtls_x509_crt_free(&crt);
  return out;
}
}  // namespace internal
namespace {

void on_view_timer(void *) { view_timer_fired(); }

void on_network_change(bool up) {
  if (up) {
    if (paired()) mqtt::start();
  }
}

}  // namespace

// --- names -------------------------------------------------------------------------

std::string channel_id(const ChannelRef &ref) {
  switch (ref.kind) {
    case content::ChannelKind::MakapixPromoted: return "promoted";
    case content::ChannelKind::MakapixAll: return "all";
    case content::ChannelKind::MakapixOwn: return "own";
    case content::ChannelKind::MakapixArtist: return "artist-" + ref.identifier;
    case content::ChannelKind::MakapixHashtag: return "hashtag-" + ref.identifier;
    case content::ChannelKind::MakapixReactions: return "reactions-" + ref.identifier;
    default: return "unknown";
  }
}

const char *server_channel_name(content::ChannelKind kind) {
  switch (kind) {
    case content::ChannelKind::MakapixPromoted: return "promoted";
    case content::ChannelKind::MakapixAll: return "all";
    case content::ChannelKind::MakapixOwn: return "user";
    case content::ChannelKind::MakapixArtist: return "by_user";
    case content::ChannelKind::MakapixHashtag: return "hashtag";
    case content::ChannelKind::MakapixReactions: return "reactions";
    default: return "all";
  }
}

// --- commands from the site ----------------------------------------------------------

namespace internal {

void handle_command(const char *json, size_t len) {
  // Parsing is contract::parse_command (pure, host-tested); this only acts.
  const contract::Command c = contract::parse_command(json, len);
  if (c.kind == contract::Command::Kind::Invalid) {
    ESP_LOGW(TAG, "%s", c.warning.c_str());
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_status.commands;
  }
  ESP_LOGI(TAG, "command %s", c.type.c_str());
  if (!c.warning.empty()) ESP_LOGW(TAG, "%s", c.warning.c_str());
  switch (c.kind) {
    case contract::Command::Kind::Next:
      if (g_hooks.next) g_hooks.next();
      break;
    case contract::Command::Kind::Back:
      if (g_hooks.previous) g_hooks.previous();
      break;
    case contract::Command::Kind::ShowArtwork: {
      auto *job = new Job{JobType::ShowArtwork};
      job->entry = c.entry;
      job->name = c.name;
      submit(job);
      break;
    }
    case contract::Command::Kind::PlayPlayset:
      if (g_hooks.play_playset) g_hooks.play_playset(c.playset);
      break;
    case contract::Command::Kind::SetPaused:
      if (g_hooks.set_paused) g_hooks.set_paused(c.paused);
      break;
    case contract::Command::Kind::SetBrightness:
      if (g_hooks.set_brightness) g_hooks.set_brightness(c.brightness);
      break;
    case contract::Command::Kind::SetRotation:
      if (g_hooks.set_rotation) g_hooks.set_rotation(c.rotation);
      break;
    case contract::Command::Kind::None:
    case contract::Command::Kind::Invalid:
      break;
  }
  if (!c.ack_status.empty()) mqtt::publish_ack(c.id, c.ack_status.c_str(), c.ack_error.empty() ? nullptr : c.ack_error.c_str());
  if (c.republish_state) mqtt::publish_state();
}

void view_timer_fired() {
  api::ViewEvent v;
  bool paired_now;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    paired_now = g_status.state == State::Paired;
    if (g_current_post < 0) return;
    v.post_id = g_current_post;
    v.intentional = g_current_intentional;
    if (g_current_has_channel) {
      v.channel = server_channel_name(g_current_channel.kind);
      if (g_current_channel.kind == content::ChannelKind::MakapixArtist ||
          g_current_channel.kind == content::ChannelKind::MakapixReactions) {
        v.user_sqid = g_current_channel.identifier;
      } else if (g_current_channel.kind == content::ChannelKind::MakapixHashtag) {
        v.hashtag = g_current_channel.identifier;
      }
    } else {
      v.channel = "all";
    }
  }
  if (!paired_now || !net::clock::synced()) return;  // views are attributed to the owner
  v.timestamp = iso_now();
  v.play_order = system::settings().pick_mode == system::PickMode::Recency ? 1 : 2;
  if (mqtt::publish_view(v)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_status.views_sent;
    return;
  }
  auto *job = new Job{JobType::View};
  job->view = v;
  if (!submit(job)) delete job;
}

}  // namespace internal

// --- public API ----------------------------------------------------------------------

bool start(const Hooks &hooks) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_hooks = hooks;
    g_status.host = CONFIG_P64_MAKAPIX_HOST;
    if (creds::load(g_creds)) {
      g_status.player_key = g_creds.player_key;
      g_status.state = g_creds.complete() ? State::Paired : State::Unpaired;
      g_status.cert_expires_at = cert_not_after(g_creds.cert_pem);
      if (!g_creds.complete()) g_creds = creds::Credentials{};
    }
  }
  const esp_timer_create_args_t args = {on_view_timer, nullptr, ESP_TIMER_TASK, "mk_view", false};
  esp_timer_create(&args, &g_view_timer);
  fetcher_start();
  system::subscribe(system::Event::WifiConnected, [](const system::Message &) { on_network_change(true); });
  system::subscribe(system::Event::TimeSynced, [](const system::Message &) { on_network_change(true); });
  g_max_side = system::settings().makapix_max_side;
  system::subscribe(system::Event::SettingsChanged, [](const system::Message &) {
    on_settings_changed();
    mqtt::publish_state();
  });
  if (paired()) {
    ESP_LOGI(TAG, "paired as %s; certificate valid until %lu", g_creds.player_key.c_str(),
             static_cast<unsigned long>(g_status.cert_expires_at));
    if (net::wifi::status().connected) mqtt::start();
  } else {
    ESP_LOGI(TAG, "not paired: the Promoted channel is available anonymously");
  }
  return true;
}

Status status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  Status s = g_status;
  s.mqtt_connected = mqtt::connected();
  return s;
}

bool paired() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status.state == State::Paired;
}

bool pair(std::string &error) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_status.state == State::Pairing) {
      error = "pairing already in progress";
      return false;
    }
  }
  if (!online()) {
    error = "the device is offline";
    return false;
  }
  mqtt::stop();
  auto *job = new Job{JobType::Provision};
  if (!submit(job)) {
    delete job;
    error = "busy";
    return false;
  }
  return true;
}

void cancel_pairing() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_status.state != State::Pairing) return;
  g_status.state = State::Unpaired;
  g_status.code.clear();
  g_status.code_expires_us = 0;
  g_creds = creds::Credentials{};
  g_status.player_key.clear();
  system::publish(system::Event::MakapixStateChanged, static_cast<int32_t>(State::Unpaired));
}

bool unpair(std::string &error) {
  mqtt::stop();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_creds = creds::Credentials{};
    g_status.player_key.clear();
    g_status.code.clear();
    g_status.cert_expires_at = 0;
    g_status.state = State::Unpaired;
  }
  if (!creds::erase()) {
    error = "credentials could not be erased";
    return false;
  }
  ESP_LOGI(TAG, "unpaired");
  system::publish(system::Event::MakapixStateChanged, static_cast<int32_t>(State::Unpaired));
  return true;
}

void set_active_channels(const std::vector<ChannelRef> &channels) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto &ch : g_channels) ch->active = false;
  for (const ChannelRef &ref : channels) ensure_channel(ref).active = true;
}

bool snapshot(const ChannelRef &ref, ChannelSnapshot &out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  Channel *ch = find_channel(channel_id(ref));
  if (!ch) return false;
  out.entries = ch->entries;
  out.cached = ch->cached;
  out.last_refresh = ch->last_refresh;
  out.oversized = ch->last_oversized;
  out.refreshing = ch->refreshing;
  out.error = ch->error;
  return true;
}

std::string artwork_path(const content::MakapixEntry &entry) { return cache::artwork_path(entry); }

bool memory_bytes(const std::string &path, std::vector<uint8_t> &out) { return cache::memory_bytes(path, out); }

void note_load_failed(const content::MakapixEntry &entry, bool missing) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto &ch : g_channels) {
    for (content::MakapixEntry &e : ch->entries) {
      if (e.post_id == entry.post_id && std::memcmp(e.storage_key, entry.storage_key, 16) == 0) {
        e.flags &= static_cast<uint8_t>(~content::kMakapixCached);
        if (!missing) e.flags |= content::kMakapixRejected;
        ch->dirty = true;
        recount_cached(*ch);
      }
    }
  }
}

void note_shown(int32_t post_id, const ChannelRef *channel, bool intentional) {
  bool publish_now = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current_post = post_id;
    g_current_has_channel = channel != nullptr;
    if (channel) g_current_channel = *channel;
    g_current_intentional = intentional;
    const int64_t now = esp_timer_get_time();
    if (now - g_last_status_us >= kStatusThrottleUs) {
      g_last_status_us = now;
      publish_now = true;
    }
  }
  if (g_view_timer) {
    esp_timer_stop(g_view_timer);
    esp_timer_start_once(g_view_timer, kViewAfterUs);
  }
  if (publish_now && mqtt::connected()) mqtt::publish_status(post_id);
}

void note_hidden() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_current_post = -1;
  if (g_view_timer) esp_timer_stop(g_view_timer);
}

bool play_post(const std::string &sqid_or_url, std::string &error) {
  std::string sqid = sqid_or_url;
  const size_t p = sqid.find("/p/");
  if (p != std::string::npos) sqid = sqid.substr(p + 3);
  while (!sqid.empty() && (sqid.back() == '/' || sqid.back() == ' ')) sqid.pop_back();
  const size_t q = sqid.find_first_of("?#");
  if (q != std::string::npos) sqid.resize(q);
  if (!content::valid_sqid(sqid)) {
    error = "not a Makapix post: give a sqid or a makapix.club/p/<sqid> link";
    return false;
  }
  if (!online()) {
    error = "the device is offline";
    return false;
  }
  auto *job = new Job{JobType::PlayPost};
  job->text = sqid;
  if (!submit(job)) {
    delete job;
    error = "busy";
    return false;
  }
  return true;
}

bool play_url(const std::string &url, std::string &error) {
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    error = "the URL must start with http:// or https://";
    return false;
  }
  if (url.size() > 512) {
    error = "URL too long";
    return false;
  }
  if (!online()) {
    error = "the device is offline";
    return false;
  }
  auto *job = new Job{JobType::PlayUrl};
  job->text = url;
  if (!submit(job)) {
    delete job;
    error = "busy";
    return false;
  }
  return true;
}

bool like(int32_t post_id, bool liked, std::string &error) {
  if (!paired()) {
    error = "likes need pairing";
    return false;
  }
  Job job{JobType::Like};
  job.number = post_id;
  job.flag = liked;
  job.done = xSemaphoreCreateBinary();
  if (!job.done) {
    error = "out of memory";
    return false;
  }
  if (!submit(&job)) {
    vSemaphoreDelete(job.done);
    error = "busy";
    return false;
  }
  const bool finished = xSemaphoreTake(job.done, pdMS_TO_TICKS(20000)) == pdTRUE;
  if (!finished) {
    // The worker still holds the pointer: never free it while the job may run. Leak the
    // semaphore rather than crash; this only happens when the server hangs.
    error = "timed out";
    return false;
  }
  vSemaphoreDelete(job.done);
  error = job.error;
  return job.ok;
}

bool cache_sweep(uint32_t older_than_s, bool dry_run, SweepResult &out, std::string &error) {
  static std::atomic<bool> busy{false};
  out = SweepResult{};
  out.dry_run = dry_run;
  out.older_than_s = older_than_s;
  if (!cache::has_card()) {
    error = "no card";
    return false;
  }
  if (!net::clock::synced()) {
    error = "the time is not trusted yet (no NTP answer since boot)";
    return false;
  }
  if (busy.exchange(true)) {
    error = "a sweep is already running";
    return false;
  }
  const int64_t t0 = esp_timer_get_time();
  const uint32_t now = epoch_now();
  // The storage keys of the cache files that went, sorted afterwards so the flag pass
  // over the loaded indexes is a binary search per entry. In PSRAM: thousands of keys.
  using Key = std::array<uint8_t, 16>;
  std::vector<Key, content::PsramAllocator<Key>> gone;
  cache::SweepStats st;
  std::string suspect;
  const bool swept = cache::sweep(now, older_than_s, net::clock::file_date_floor(), dry_run, st,
                                  [&](const std::string &name) {
                                    Key k;
                                    if (name.size() >= 36 && content::parse_uuid(name.substr(0, 36).c_str(), k.data())) {
                                      gone.push_back(k);
                                    }
                                  },
                                  suspect);
  if (!swept) {
    busy = false;
    error = suspect.empty() ? "no card" : "the clock or the card is suspect: " + suspect;
    ESP_LOGW(TAG, "cache sweep refused: %s", error.c_str());
    return false;
  }
  uint32_t unflagged = 0;
  if (!dry_run && !gone.empty()) {
    std::sort(gone.begin(), gone.end());
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &ch : g_channels) {
      if (!ch->loaded) continue;
      bool changed = false;
      for (content::MakapixEntry &e : ch->entries) {
        if (!(e.flags & content::kMakapixCached)) continue;
        Key k;
        std::memcpy(k.data(), e.storage_key, 16);
        if (!std::binary_search(gone.begin(), gone.end(), k)) continue;
        e.flags &= static_cast<uint8_t>(~content::kMakapixCached);
        changed = true;
        ++unflagged;
      }
      if (changed) {
        ch->dirty = true;
        recount_cached(*ch);
      }
    }
  }
  out.examined = st.examined;
  out.deleted = st.deleted;
  out.indexes_deleted = st.indexes_deleted;
  out.downloads_deleted = st.downloads_deleted;
  out.bytes = st.bytes;
  out.freed = st.freed;
  out.took_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.cache_files = dry_run ? st.examined : st.examined - st.deleted;
    g_status.cache_bytes = dry_run ? st.bytes : st.bytes - st.freed;
    if (!dry_run) {
      g_status.last_sweep = now;
      g_status.last_sweep_deleted = st.deleted;
      g_status.last_sweep_freed = st.freed;
    }
  }
  ESP_LOGI(TAG, "cache sweep%s: %u files (%llu KB) examined, %u older than %lu s deleted (%llu KB, %u indexes, %u downloads), "
           "%u entries unflagged, %lu ms",
           dry_run ? " (dry run)" : "", static_cast<unsigned>(st.examined), static_cast<unsigned long long>(st.bytes / 1024),
           static_cast<unsigned>(st.deleted), static_cast<unsigned long>(older_than_s),
           static_cast<unsigned long long>(st.freed / 1024), static_cast<unsigned>(st.indexes_deleted),
           static_cast<unsigned>(st.downloads_deleted), static_cast<unsigned>(unflagged), static_cast<unsigned long>(out.took_ms));
  busy = false;
  if (unflagged) publish_channel_changed();
  return true;
}

bool play_followed(std::string &error) {
  if (!paired()) {
    error = "the Followed playset needs pairing";
    return false;
  }
  auto *job = new Job{JobType::Followed};
  if (!submit(job)) {
    delete job;
    error = "busy";
    return false;
  }
  return true;
}

}  // namespace p64::makapix

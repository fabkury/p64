#include "p64/makapix/makapix.hpp"

#include <cstring>
#include <ctime>

#include "cJSON.h"
#include "cache.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "internal.hpp"
#include "mbedtls/x509_crt.h"
#include "mqtt.hpp"
#include "p64/content/playset_json.hpp"
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
  const time_t t = time(nullptr);
  return t > 0 ? static_cast<uint32_t>(t) : 0;
}

std::string iso_now() {
  const time_t t = time(nullptr);
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

std::string str(const cJSON *obj, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
}

double num(const cJSON *obj, const char *key, double fallback) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return (v && cJSON_IsNumber(v)) ? v->valuedouble : fallback;
}

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
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!root) {
    ESP_LOGW(TAG, "command: not JSON");
    return;
  }
  const std::string id = str(root, "command_id");
  const std::string type = str(root, "command_type");
  const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_status.commands;
  }
  ESP_LOGI(TAG, "command %s", type.c_str());
  if (type == "swap_next") {
    if (g_hooks.next) g_hooks.next();
  } else if (type == "swap_back") {
    if (g_hooks.previous) g_hooks.previous();
  } else if (type == "show_artwork") {
    content::MakapixEntry e = {};
    e.post_id = static_cast<int32_t>(num(payload, "post_id", -1));
    const std::string key = str(payload, "storage_key");
    const std::string shard = str(payload, "storage_shard");
    const content::MakapixFormat format = content::makapix_format_from_name(str(payload, "native_format"));
    if (e.post_id >= 0 && content::parse_uuid(key.c_str(), e.storage_key) && format != content::MakapixFormat::Unknown) {
      std::strncpy(e.shard, shard.c_str(), sizeof(e.shard) - 1);
      e.format = static_cast<uint8_t>(format);
      e.width = static_cast<uint16_t>(num(payload, "width", 0));
      e.height = static_cast<uint16_t>(num(payload, "height", 0));
      auto *job = new Job{JobType::ShowArtwork};
      job->entry = e;
      job->name = "post " + std::to_string(e.post_id);
      submit(job);
    } else {
      ESP_LOGW(TAG, "show_artwork: incomplete payload");
    }
  } else if (type == "play_channel") {
    const std::string name = str(payload, "channel_name");
    content::ChannelSpec spec;
    bool ok = true;
    if (name == "all") {
      spec.kind = content::ChannelKind::MakapixAll;
    } else if (name == "promoted") {
      spec.kind = content::ChannelKind::MakapixPromoted;
    } else if (name == "user") {
      spec.kind = content::ChannelKind::MakapixOwn;
    } else if (name == "by_user") {
      spec.kind = content::ChannelKind::MakapixArtist;
      spec.identifier = str(payload, "user_sqid");
      spec.display_name = str(payload, "user_handle");
      if (!spec.display_name.empty()) spec.display_name = "@" + spec.display_name;
    } else if (name == "hashtag") {
      spec.kind = content::ChannelKind::MakapixHashtag;
      spec.identifier = str(payload, "hashtag");
    } else {
      ok = false;
    }
    std::string e;
    if (ok && spec.validate(e)) {
      content::Playset p;
      p.name = "Makapix";
      p.channels.push_back(spec);
      if (g_hooks.play_playset) g_hooks.play_playset(p);
    } else {
      ESP_LOGW(TAG, "play_channel: unusable payload (%s)", e.c_str());
    }
  } else if (type == "play_playset") {
    content::Playset p;
    std::string e;
    if (content::playset_from_json(payload, p, e)) {
      const std::string name = str(payload, "playset_name");
      p.name = name.empty() ? "Makapix" : name;
      content::Builtin b;
      p.builtin = content::builtin_from_name(p.name, b) || p.name == "followed_artists";
      if (p.name == "followed_artists") p.name = content::builtin_name(content::Builtin::Followed);
      // Drop channels this device cannot play (sdcard from a p3a, reserved kinds).
      for (auto it = p.channels.begin(); it != p.channels.end();) {
        std::string ce;
        if (!it->supported() || !it->validate(ce)) {
          it = p.channels.erase(it);
        } else {
          ++it;
        }
      }
      if (!p.channels.empty() && g_hooks.play_playset) {
        g_hooks.play_playset(p);
      } else {
        ESP_LOGW(TAG, "play_playset: no playable channels");
      }
    } else {
      ESP_LOGW(TAG, "play_playset: %s", e.c_str());
    }
  } else if (type == "set_paused") {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(payload, "paused");
    if (v && cJSON_IsBool(v)) {
      if (g_hooks.set_paused) g_hooks.set_paused(cJSON_IsTrue(v));
      mqtt::publish_ack(id, "ok", nullptr);
      mqtt::publish_state();
    } else {
      mqtt::publish_ack(id, "error", "missing or invalid 'paused' field");
    }
  } else if (type == "set_brightness") {
    const double v = num(payload, "value", -1);
    if (v >= 1 && v <= 255) {
      if (g_hooks.set_brightness) g_hooks.set_brightness(static_cast<uint8_t>(v));
      mqtt::publish_ack(id, "ok", nullptr);
      mqtt::publish_state();
    } else {
      mqtt::publish_ack(id, "error", "brightness must be 1 to 255");
    }
  } else if (type == "set_rotation") {
    const double v = num(payload, "value", -1);
    if (v == 0 || v == 90 || v == 180 || v == 270) {
      if (g_hooks.set_rotation) g_hooks.set_rotation(static_cast<uint16_t>(v));
      mqtt::publish_ack(id, "ok", nullptr);
      mqtt::publish_state();
    } else {
      mqtt::publish_ack(id, "error", "rotation must be 0, 90, 180, or 270");
    }
  } else if (type == "set_mirror") {
    mqtt::publish_ack(id, "unsupported", nullptr);
  } else {
    ESP_LOGW(TAG, "command %s: unknown", type.c_str());
    if (!id.empty()) mqtt::publish_ack(id, "unsupported", nullptr);
  }
  cJSON_Delete(root);
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

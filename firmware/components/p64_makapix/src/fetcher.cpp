// The worker task: every HTTPS request in turn (one TLS session at a time, ADR 0009):
// pairing, channel refreshes, artwork downloads, play-this downloads, likes, views over
// HTTPS, certificate renewal. Core 0, priority 4, internal stack (TLS runs here).
#include <algorithm>
#include <cstring>
#include <ctime>

#include "cache.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "internal.hpp"
#include "mqtt.hpp"
#include "p64/content/makapix_index.hpp"
#include "p64/decode/decoder.hpp"
#include "p64/playback/artwork.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"

namespace p64::makapix::internal {
namespace {

constexpr int64_t kSecond = 1000000;
constexpr int64_t kPollIntervalUs = 3 * kSecond;
constexpr int64_t kRetryBaseUs = 30 * kSecond;
constexpr int64_t kRetryMaxUs = 15 * 60 * kSecond;
constexpr uint64_t kFreeSpaceFloor = 64ULL * 1024 * 1024;  // downloads stop below this (spec 5.4)
constexpr uint32_t kSaveEveryDownloads = 8;
constexpr int64_t kRenewalCheckUs = 24LL * 3600 * kSecond;
constexpr uint32_t kRenewalWindowDays = 45;

QueueHandle_t g_jobs = nullptr;
size_t g_round_robin = 0;
net::fetch::Session g_vault;  // the file host connection, kept open across downloads
int64_t g_vault_used_us = 0;
constexpr int64_t kVaultIdleUs = 20 * kSecond;
constexpr int64_t kWalkIdleUs = 60 * kSecond;  // a walk paused longer (its channel left the playset) restarts
uint32_t g_unsaved_downloads = 0;
int64_t g_next_renewal_check_us = 0;

std::string sanitise_name(const std::string &raw) {
  std::string out;
  for (char c : raw) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    out += ok ? c : '_';
    if (out.size() >= 60) break;
  }
  if (out.empty() || out[0] == '.') out = "download" + out;
  return out;
}

void finish(Job *job, bool ok, const std::string &error) {
  job->ok = ok;
  job->error = error;
  if (!ok && !error.empty()) set_error(error);
  if (job->done) {
    xSemaphoreGive(job->done);  // the submitter owns the job
  } else {
    delete job;
  }
}

// --- pairing --------------------------------------------------------------------------

void run_provision(Job *job) {
  set_activity("asking for a pairing code");
  api::Provision p;
  std::string error;
  if (!api::provision(p, error)) {
    set_activity("");
    set_state(State::Unpaired);
    finish(job, false, error);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_creds = creds::Credentials{};
    g_creds.player_key = p.player_key;
    g_creds.mqtt_host = p.mqtt_host;
    g_creds.mqtt_port = p.mqtt_port;
    g_creds.https_base = p.https_base;
    g_status.player_key = p.player_key;
    g_status.code = p.code;
    g_status.code_expires_us = esp_timer_get_time() + 15 * 60 * kSecond;
    g_status.last_error.clear();
    g_next_poll_us = esp_timer_get_time() + kPollIntervalUs;
  }
  ESP_LOGI(TAG, "pairing code %s (valid 15 min); enter it on the site", p.code.c_str());
  set_activity("");
  set_state(State::Pairing);
  finish(job, true, "");
}

void poll_credentials() {
  std::string key;
  int64_t expires;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    key = g_creds.player_key;
    expires = g_status.code_expires_us;
    g_next_poll_us = esp_timer_get_time() + kPollIntervalUs;
  }
  if (key.empty()) return;
  creds::Credentials c;
  std::string error;
  const int r = api::credentials(key, c, error);
  if (r == 1) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (c.mqtt_host.empty()) c.mqtt_host = g_creds.mqtt_host;
      if (c.mqtt_port == 0) c.mqtt_port = g_creds.mqtt_port;
      if (c.https_base.empty()) c.https_base = g_creds.https_base;
      g_creds = c;
      g_status.code.clear();
      g_status.code_expires_us = 0;
      g_status.cert_expires_at = cert_not_after(c.cert_pem);
    }
    creds::save(c);
    ESP_LOGI(TAG, "paired: credentials stored%s", c.api_token.empty() ? " (no API token in the reply)" : "");
    set_state(State::Paired);
    mqtt::start();
    return;
  }
  if (r < 0) ESP_LOGW(TAG, "credentials: %s", error.c_str());
  if (esp_timer_get_time() >= expires) {
    ESP_LOGW(TAG, "the pairing code expired");
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_status.code.clear();
      g_status.code_expires_us = 0;
      g_creds = creds::Credentials{};
      g_status.player_key.clear();
    }
    set_error("the pairing code expired; start again");
    set_state(State::Unpaired);
  }
}

// --- channel refresh ---------------------------------------------------------------

Channel *next_channel_needing_service() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const int64_t now = esp_timer_get_time();
  for (auto &ch : g_channels) {
    if (ch->active && ch->refreshing) return ch.get();  // a page walk in progress
  }
  for (auto &ch : g_channels) {
    if (!ch->active) continue;
    if (!ch->loaded) return ch.get();
    if (ch->retry_at_us && now < ch->retry_at_us) continue;
    if (ch->next_refresh_us == 0 || now >= ch->next_refresh_us) return ch.get();
  }
  return nullptr;
}

void load_channel(Channel *ch) {
  content::MakapixEntries entries;
  uint32_t last_refresh = 0;
  const bool loaded = cache::load_index(ch->id, entries, last_refresh);
  const uint32_t interval = system::settings().makapix_refresh_seconds;
  const uint32_t now = epoch_now();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ch->loaded = true;
    if (loaded) {
      ch->entries = std::move(entries);
      ch->last_refresh = last_refresh;
      recount_cached(*ch);
      // Fresh enough: the next refresh waits for the interval; else refresh now.
      const int64_t age = now > last_refresh ? static_cast<int64_t>(now - last_refresh) : 0;
      ch->next_refresh_us = age >= interval ? 0 : esp_timer_get_time() + (interval - age) * kSecond;
      ESP_LOGI(TAG, "channel %s: %u entries from the card, %u cached, %lld s old", ch->id.c_str(),
               static_cast<unsigned>(ch->entries.size()), static_cast<unsigned>(ch->cached), static_cast<long long>(age));
    }
  }
  publish_channel_changed();
}

void finish_walk(Channel *ch, bool ok, const std::string &error) {
  const uint32_t interval = system::settings().makapix_refresh_seconds;
  bool save = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ch->refreshing = false;
    ch->walk_session.reset();
    ch->walk_cursor.clear();
    if (ok) {
      content::MakapixEntries fresh = std::move(ch->walk_fresh);
      ch->walk_fresh = content::MakapixEntries();
      const size_t dropped = content::merge_index(ch->entries, fresh);
      ch->entries = std::move(fresh);
      recount_cached(*ch);
      ch->last_refresh = epoch_now();
      ch->next_refresh_us = esp_timer_get_time() + static_cast<int64_t>(interval) * kSecond;
      ch->retry_at_us = 0;
      ch->fail_streak = 0;
      ch->dirty = true;
      if (ch->download_cursor >= ch->entries.size()) ch->download_cursor = 0;
      ++g_status.refreshes;
      save = true;
      ESP_LOGI(TAG, "channel %s: %u entries in %lu pages (%u cached, %u dropped, %u over the size limit)", ch->id.c_str(),
               static_cast<unsigned>(ch->entries.size()), static_cast<unsigned long>(ch->walk_pages),
               static_cast<unsigned>(ch->cached), static_cast<unsigned>(dropped), static_cast<unsigned>(ch->walk_oversized));
      if (ch->rewalk) ch->next_refresh_us = 0;  // the size limit changed while this walk ran
    } else {
      ch->walk_fresh = content::MakapixEntries();
      ++ch->fail_streak;
      int64_t wait = kRetryBaseUs;
      for (uint32_t i = 1; i < ch->fail_streak && wait < kRetryMaxUs; ++i) wait *= 2;
      if (error == "needs pairing") wait = kRetryMaxUs;
      ch->retry_at_us = esp_timer_get_time() + std::min(wait, kRetryMaxUs);
      ch->error = error;
      ESP_LOGW(TAG, "channel %s: refresh failed: %s (retry in %lld s)", ch->id.c_str(), error.c_str(),
               static_cast<long long>(std::min(wait, kRetryMaxUs) / kSecond));
    }
    ch->walk_pages = 0;
    ch->walk_oversized = 0;
    ch->rewalk = false;
  }
  if (save) {
    content::MakapixEntries copy;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      copy = ch->entries;
      ch->dirty = false;
    }
    std::string e;
    if (cache::has_card() && !cache::save_index(ch->id, copy, e)) ESP_LOGW(TAG, "index %s not saved: %s", ch->id.c_str(), e.c_str());
  } else {
    set_error(ch->id + ": " + error);
  }
  set_activity("");
  publish_channel_changed();
}

// One page of a channel refresh. The first call starts the walk; the last merges it in.
void refresh_step(Channel *ch) {
  ChannelRef ref;
  std::string token, cursor;
  bool paired, first;
  size_t have;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ref = ch->ref;
    token = g_creds.api_token;
    paired = g_status.state == State::Paired && !token.empty();
    // A walk pauses while its channel is out of the playset; resumed much later, its
    // kept-alive connection is dead (ESP_ERR_HTTP_WRITE_DATA, seen 2026-09-21) and its
    // cursor may be stale, so it starts over.
    const bool stale = ch->refreshing && esp_timer_get_time() - ch->walk_last_us > kWalkIdleUs;
    if (stale) ESP_LOGI(TAG, "channel %s: refresh paused too long; starting over", ch->id.c_str());
    first = !ch->refreshing || stale;
    ch->walk_last_us = esp_timer_get_time();
    if (first) {
      ch->refreshing = true;
      ch->error.clear();
      ch->walk_cursor.clear();
      ch->walk_fresh.clear();
      ch->walk_pages = 0;
      ch->walk_oversized = 0;
      ch->walk_session = std::make_unique<net::fetch::Session>();
    }
    cursor = ch->walk_cursor;
    have = ch->walk_fresh.size();
  }
  if (!paired && ref.kind != content::ChannelKind::MakapixPromoted) {
    finish_walk(ch, false, "needs pairing");
    return;
  }
  const size_t cap = system::settings().channel_cache_size;
  const uint16_t max_side = system::settings().makapix_max_side;
  set_activity("refreshing " + ch->id + " (page " + std::to_string(ch->walk_pages + 1) + ")");
  content::MakapixEntries page;
  std::string next, error;
  bool more = false;
  const bool ok = paired ? api::query_page(token, ref, max_side, cursor, page, next, more, error, ch->walk_session.get())
                         : api::promoted_page(cursor, page, next, more, error, ch->walk_session.get());
  if (!ok) {
    finish_walk(ch, false, error);
    return;
  }
  // The size limit, on the device as well: the promoted feed has no size filter, and a
  // listing that ignored the criteria must not fill the index with what cannot play.
  const bool listed_none = page.empty();
  const size_t listed = page.size();
  page.erase(std::remove_if(page.begin(), page.end(),
                            [max_side](const content::MakapixEntry &e) { return !content::fits_side(e, max_side); }),
             page.end());
  bool done;
  bool cold;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ch->walk_oversized += static_cast<uint32_t>(listed - page.size());
    ch->walk_fresh.insert(ch->walk_fresh.end(), page.begin(), page.end());
    if (ch->walk_fresh.size() > cap) ch->walk_fresh.resize(cap);
    ++ch->walk_pages;
    ch->walk_cursor = next;
    done = !more || ch->walk_fresh.size() >= cap || listed_none;
    cold = ch->entries.empty();
    if (!done && cold) {
      // Nothing to play yet: let the first pages count so downloads start now.
      ch->entries = ch->walk_fresh;
      recount_cached(*ch);
    }
  }
  (void)have;
  if (done) {
    finish_walk(ch, true, "");
  } else if (cold) {
    publish_channel_changed();
  }
}

// --- downloads ----------------------------------------------------------------------

// Picks the next entry to fetch, round-robin across the active channels.
bool next_download(Channel *&channel, content::MakapixEntry &entry, size_t &index) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_channels.empty()) return false;
  const size_t n = g_channels.size();
  for (size_t k = 0; k < n; ++k) {
    Channel &ch = *g_channels[(g_round_robin + k) % n];
    if (!ch.active || !ch.loaded || ch.entries.empty()) continue;
    for (size_t t = 0; t < ch.entries.size(); ++t) {
      const size_t i = (ch.download_cursor + t) % ch.entries.size();
      const content::MakapixEntry &e = ch.entries[i];
      if (e.flags & (content::kMakapixCached | content::kMakapixMissing | content::kMakapixRejected)) continue;
      channel = &ch;
      entry = e;
      index = i;
      ch.download_cursor = static_cast<uint32_t>((i + 1) % ch.entries.size());
      g_round_robin = (g_round_robin + k + 1) % n;
      return true;
    }
  }
  return false;
}

void flag_entry(Channel *ch, size_t index, const content::MakapixEntry &entry, uint8_t flag) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (index < ch->entries.size() && ch->entries[index].post_id == entry.post_id) {
    ch->entries[index].flags |= flag;
    ch->dirty = true;
    recount_cached(*ch);
  }
}

void save_dirty_indexes() {
  std::vector<std::pair<std::string, content::MakapixEntries>> work;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &ch : g_channels) {
      if (ch->dirty && ch->loaded) {
        work.emplace_back(ch->id, ch->entries);
        ch->dirty = false;
      }
    }
  }
  for (auto &w : work) {
    std::string e;
    if (cache::has_card() && !cache::save_index(w.first, w.second, e)) ESP_LOGW(TAG, "index %s not saved: %s", w.first.c_str(), e.c_str());
  }
  g_unsaved_downloads = 0;
}

bool download_step() {
  Channel *ch = nullptr;
  content::MakapixEntry entry;
  size_t index = 0;
  if (!next_download(ch, entry, index)) {
    if (g_unsaved_downloads) save_dirty_indexes();
    return false;
  }
  if (cache::has_card() && cache::free_bytes() < kFreeSpaceFloor) {
    static int64_t last_warning = 0;
    if (esp_timer_get_time() - last_warning > 60 * kSecond) {
      last_warning = esp_timer_get_time();
      ESP_LOGW(TAG, "card below the free-space floor; downloads paused");
    }
    return false;
  }
  if (cache::artwork_present(entry)) {  // already there (an index rebuilt without its flags)
    flag_entry(ch, index, entry, content::kMakapixCached);
    publish_channel_changed();
    return true;
  }
  set_activity("downloading post " + std::to_string(entry.post_id));
  std::vector<uint8_t> bytes;
  bool missing = false;
  std::string error;
  const std::string url = api::download_url(entry);
  g_vault_used_us = esp_timer_get_time();
  if (!api::download(url, bytes, playback::kMaxFileBytes, missing, error, &g_vault)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_status.download_failures;
    if (missing) {
      if (index < ch->entries.size() && ch->entries[index].post_id == entry.post_id) {
        ch->entries[index].flags |= content::kMakapixMissing;
        ch->dirty = true;
      }
    } else {
      // A transport failure: leave the entry for the next round, but slow down.
      ch->error = error;
    }
    ESP_LOGW(TAG, "post %ld: %s", static_cast<long>(entry.post_id), error.c_str());
    set_activity("");
    if (!missing) vTaskDelay(pdMS_TO_TICKS(5000));
    return true;
  }
  const decode::Format format = decode::sniff(bytes.data(), bytes.size());
  if (format == decode::Format::Unknown) {
    flag_entry(ch, index, entry, content::kMakapixRejected);
    ESP_LOGW(TAG, "post %ld: not a GIF, PNG, WebP or BMP", static_cast<long>(entry.post_id));
    set_activity("");
    return true;
  }
  if (!cache::store_artwork(entry, bytes, error)) {
    ESP_LOGW(TAG, "post %ld: cannot store: %s", static_cast<long>(entry.post_id), error.c_str());
    set_error(error);
    set_activity("");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return true;
  }
  flag_entry(ch, index, entry, content::kMakapixCached);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_status.downloads;
  }
  ESP_LOGI(TAG, "cached post %ld (%u bytes, %s) for %s", static_cast<long>(entry.post_id), static_cast<unsigned>(bytes.size()),
           decode::format_name(format), ch->id.c_str());
  if (++g_unsaved_downloads >= kSaveEveryDownloads) save_dirty_indexes();
  set_activity("");
  publish_channel_changed();
  return true;
}

// --- play-this --------------------------------------------------------------------

void play_downloaded(Job *job, const content::MakapixEntry &entry, int32_t post_id, const std::string &name) {
  std::string path;
  std::string error;
  if (cache::artwork_present(entry)) {
    path = cache::artwork_path(entry);
  } else {
    set_activity("downloading post " + std::to_string(post_id));
    std::vector<uint8_t> bytes;
    bool missing = false;
    if (!api::download(api::download_url(entry), bytes, playback::kMaxFileBytes, missing, error)) {
      set_activity("");
      finish(job, false, error);
      return;
    }
    if (decode::sniff(bytes.data(), bytes.size()) == decode::Format::Unknown) {
      set_activity("");
      finish(job, false, "post " + std::to_string(post_id) + ": not an artwork file");
      return;
    }
    const std::string file = content::format_uuid(entry.storage_key) +
                             content::makapix_format_ext(static_cast<content::MakapixFormat>(entry.format));
    path = cache::store_download(file, bytes, error);
    set_activity("");
    if (path.empty()) {
      finish(job, false, error);
      return;
    }
  }
  if (g_hooks.play_artwork) g_hooks.play_artwork(path, post_id, name);
  finish(job, true, "");
}

void run_play_post(Job *job) {
  set_activity("looking up " + job->text);
  content::MakapixEntry entry;
  std::string title, error;
  if (!api::post_by_sqid(job->text, entry, title, error)) {
    set_activity("");
    finish(job, false, error);
    return;
  }
  std::strncpy(entry.sqid, job->text.c_str(), sizeof(entry.sqid) - 1);
  play_downloaded(job, entry, entry.post_id, title.empty() ? job->text : title);
}

void run_play_url(Job *job) {
  set_activity("downloading " + job->text);
  std::vector<uint8_t> bytes;
  bool missing = false;
  std::string error;
  if (!api::download(job->text, bytes, playback::kMaxFileBytes, missing, error)) {
    set_activity("");
    finish(job, false, error);
    return;
  }
  const decode::Format format = decode::sniff(bytes.data(), bytes.size());
  if (format == decode::Format::Unknown) {
    set_activity("");
    finish(job, false, "not a GIF, PNG, WebP or BMP");
    return;
  }
  std::string name = job->text.substr(job->text.rfind('/') + 1);
  const size_t q = name.find('?');
  if (q != std::string::npos) name.resize(q);
  name = sanitise_name(name);
  if (name.rfind('.') == std::string::npos) {
    name += format == decode::Format::Gif ? ".gif" : format == decode::Format::WebP ? ".webp" : format == decode::Format::Bmp ? ".bmp" : ".png";
  }
  const std::string path = cache::store_download(name, bytes, error);
  set_activity("");
  if (path.empty()) {
    finish(job, false, error);
    return;
  }
  if (g_hooks.play_artwork) g_hooks.play_artwork(path, -1, name);
  finish(job, true, "");
}

void run_like(Job *job) {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    token = g_creds.api_token;
  }
  if (token.empty()) {
    finish(job, false, "likes need pairing");
    return;
  }
  set_activity(job->flag ? "liking" : "unliking");
  std::string error;
  const bool ok = api::reaction(token, job->number, job->flag, error);
  set_activity("");
  finish(job, ok, error);
}

void run_view(Job *job) {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    token = g_creds.api_token;
  }
  if (token.empty()) {
    finish(job, false, "");
    return;
  }
  std::string error;
  const bool ok = api::view(token, job->view, error);
  if (ok) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_status.views_sent;
  } else {
    ESP_LOGW(TAG, "view event: %s", error.c_str());
  }
  finish(job, ok, "");
}

void run_followed(Job *job) {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    token = g_creds.api_token;
  }
  if (token.empty()) {
    finish(job, false, "needs pairing");
    return;
  }
  set_activity("fetching the followed artists");
  content::Playset p;
  std::string error;
  const bool ok = api::get_playset(token, "followed_artists", p, error);
  set_activity("");
  if (!ok) {
    finish(job, false, error);
    return;
  }
  p.name = content::builtin_name(content::Builtin::Followed);
  p.builtin = true;
  if (g_hooks.play_playset) g_hooks.play_playset(p);
  finish(job, true, "");
}

void run_renew(Job *job) {
  creds::Credentials c;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    c = g_creds;
  }
  if (c.api_token.empty()) {
    finish(job, false, "renewal needs the API token");
    return;
  }
  set_activity("renewing the certificate");
  uint32_t expires = 0;
  std::string error;
  bool ok = api::renew_cert(c.api_token, c, expires, error);
  if (!ok && error.find("HTTP 401") != std::string::npos) {
    std::string token;
    if (api::rotate_token(c.player_key, token, error)) {
      c.api_token = token;
      creds::save_token(token);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_creds.api_token = token;
      }
      ok = api::renew_cert(c.api_token, c, expires, error);
    }
  }
  set_activity("");
  if (!ok) {
    finish(job, false, error);
    return;
  }
  creds::save_pems(c.ca_pem, c.cert_pem, c.key_pem);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_creds.ca_pem = c.ca_pem;
    g_creds.cert_pem = c.cert_pem;
    g_creds.key_pem = c.key_pem;
    g_status.cert_expires_at = expires;
  }
  ESP_LOGI(TAG, "certificate renewed; valid until %lu", static_cast<unsigned long>(expires));
  mqtt::stop();
  mqtt::start();
  finish(job, true, "");
}

bool renewal_due() {
  uint32_t expires;
  State state;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    expires = g_status.cert_expires_at;
    state = g_status.state;
  }
  if (state != State::Paired || expires == 0) return false;
  const uint32_t now = epoch_now();
  return now + kRenewalWindowDays * 86400 >= expires;
}

void run_job(Job *job) {
  switch (job->type) {
    case JobType::Provision: run_provision(job); break;
    case JobType::PlayPost: run_play_post(job); break;
    case JobType::PlayUrl: run_play_url(job); break;
    case JobType::ShowArtwork: play_downloaded(job, job->entry, job->entry.post_id, job->name); break;
    case JobType::Like: run_like(job); break;
    case JobType::View: run_view(job); break;
    case JobType::Followed: run_followed(job); break;
    case JobType::Renew: run_renew(job); break;
  }
}

void task(void *) {
  g_next_renewal_check_us = esp_timer_get_time() + 90 * kSecond;
  while (true) {
    Job *job = nullptr;
    if (xQueueReceive(g_jobs, &job, pdMS_TO_TICKS(500)) == pdTRUE && job) {
      if (online() || job->type == JobType::Like) {
        run_job(job);
      } else {
        finish(job, false, "offline");
      }
      continue;
    }
    const bool up = online();
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_status.online = up;
    }
    if (!up) continue;
    State state;
    int64_t next_poll;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      state = g_status.state;
      next_poll = g_next_poll_us;
    }
    if (state == State::Pairing && esp_timer_get_time() >= next_poll) {
      poll_credentials();
      continue;
    }
    if (esp_timer_get_time() >= g_next_renewal_check_us) {
      g_next_renewal_check_us = esp_timer_get_time() + kRenewalCheckUs;
      if (renewal_due()) {
        auto *renew = new Job{JobType::Renew};
        run_job(renew);
        continue;
      }
    }
    if (Channel *ch = next_channel_needing_service()) {
      if (!ch->loaded) {
        load_channel(ch);
        continue;
      }
      refresh_step(ch);  // one page; downloads get their turn below
    }
    const bool downloaded = download_step();
    if (g_vault.open() && esp_timer_get_time() - g_vault_used_us > kVaultIdleUs) g_vault.close();
    if (!downloaded) vTaskDelay(pdMS_TO_TICKS(500));
  }
}

}  // namespace

bool submit(Job *job) {
  if (!g_jobs || xQueueSend(g_jobs, &job, pdMS_TO_TICKS(200)) != pdTRUE) {
    ESP_LOGW(TAG, "worker queue full");
    return false;
  }
  return true;
}

void fetcher_start() {
  if (g_jobs) return;
  g_jobs = xQueueCreate(8, sizeof(Job *));
  // Internal stack: TLS handshakes and the hardware crypto run on this task.
  // PSRAM stack: TLS and card I/O; its NVS writes run through the flash guard.
  xTaskCreatePinnedToCoreWithCaps(task, "makapix", 12288, nullptr, 4, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

}  // namespace p64::makapix::internal

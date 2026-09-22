// State and helpers shared by the Makapix component's files. Everything mutable lives
// under g_mutex; the worker task (fetcher.cpp) does the network and card I/O outside it.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api.hpp"
#include "credentials.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "p64/makapix/makapix.hpp"
#include "p64/net/fetch.hpp"

namespace p64::makapix::internal {

constexpr const char *TAG = "makapix";

struct Channel {
  ChannelRef ref;
  std::string id;
  content::MakapixEntries entries;
  uint32_t cached = 0;
  uint32_t last_refresh = 0;    // epoch seconds
  int64_t next_refresh_us = 0;  // monotonic; 0 = as soon as possible
  int64_t retry_at_us = 0;      // after a failure
  uint32_t fail_streak = 0;
  bool refreshing = false;
  bool loaded = false;  // the index file was looked for
  bool dirty = false;   // flags changed since the last save
  bool active = false;  // in the active playset
  bool rewalk = false;  // the size limit changed during a walk: walk again as soon as it ends
  uint32_t download_cursor = 0;
  std::string error;
  // A refresh in progress: one page per worker step, the connection kept open.
  std::string walk_cursor;
  content::MakapixEntries walk_fresh;
  uint32_t walk_pages = 0;
  uint32_t walk_oversized = 0;  // listed entries over the size limit, dropped
  int64_t walk_last_us = 0;     // monotonic, the last page; a walk paused longer than kWalkIdleUs starts over
  std::unique_ptr<net::fetch::Session> walk_session;
};

extern std::mutex g_mutex;
extern Status g_status;
extern creds::Credentials g_creds;
extern Hooks g_hooks;
extern std::vector<std::unique_ptr<Channel>> g_channels;
extern int64_t g_next_poll_us;  // pairing: when to poll for the credentials next

// g_mutex must be held.
Channel *find_channel(const std::string &id);
Channel &ensure_channel(const ChannelRef &ref);
void recount_cached(Channel &ch);

void set_state(State state);
void set_error(const std::string &error);
void set_activity(const std::string &activity);
void publish_channel_changed();
bool online();  // Wi-Fi connected and the clock synced
uint32_t epoch_now();
std::string iso_now();
// notAfter of a PEM certificate as epoch seconds (0 when unparsable).
uint32_t cert_not_after(const std::string &pem);

// Jobs for the worker.
enum class JobType : uint8_t { Provision, PlayPost, PlayUrl, ShowArtwork, Like, View, Followed, Renew };
struct Job {
  explicit Job(JobType t) : type(t) {}
  JobType type;
  std::string text;  // sqid, URL, ...
  int32_t number = 0;
  bool flag = false;
  api::ViewEvent view;
  content::MakapixEntry entry = {};
  std::string name;
  SemaphoreHandle_t done = nullptr;  // when set, the submitter waits and owns the job
  bool ok = false;
  std::string error;
};
bool submit(Job *job);
void fetcher_start();

// Commands from the site (mqtt.cpp hands them here).
void handle_command(const char *json, size_t len);
// Playback reporting from the show, used by the view timer.
void view_timer_fired();

}  // namespace p64::makapix::internal

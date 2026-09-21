// p64 -- Makapix Club (spec section 13, ADR 0009): pairing, credentials, the channel
// indexes and the artwork cache, downloads, the MQTT session for commands and presence,
// views and likes. One worker task on core 0 does every HTTPS request in turn (one TLS
// session at a time next to the persistent MQTT one); the show reads snapshots and
// hears about changes on the event bus (MakapixStateChanged, MakapixChannelChanged).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "p64/content/makapix_index.hpp"
#include "p64/content/playset.hpp"

namespace p64::makapix {

enum class State : uint8_t {
  Unpaired = 0,  // the Promoted channel works anonymously; everything else needs pairing
  Pairing,       // a code is on display; polling for the credentials
  Paired,        // credentials stored; MQTT connects when the network is up
  Invalid,       // the server keeps refusing the credentials: re-pair from the web UI
};

struct Status {
  State state = State::Unpaired;
  std::string player_key;
  std::string code;         // the registration code while pairing
  int64_t code_expires_us;  // monotonic; 0 when none
  bool online = false;      // Wi-Fi up and the clock synced: requests can be made
  bool mqtt_connected = false;
  std::string activity;     // what the worker is doing right now ("" when idle)
  std::string last_error;
  uint32_t cert_expires_at = 0;  // epoch seconds, 0 unknown
  uint32_t refreshes = 0, downloads = 0, download_failures = 0, views_sent = 0, commands = 0;
  std::string host;
  // The card cache as the last sweep (or dry run) saw it: files under cache/, downloads/
  // and channels/ and their bytes; when the last real sweep ran and what it removed.
  uint32_t cache_files = 0;
  uint64_t cache_bytes = 0;
  uint32_t last_sweep = 0;  // epoch seconds, 0 never
  uint32_t last_sweep_deleted = 0;
  uint64_t last_sweep_freed = 0;
};

struct ChannelRef {
  content::ChannelKind kind = content::ChannelKind::MakapixPromoted;
  std::string identifier;
};
// "promoted", "all", "own", "artist-<sqid>", "hashtag-<tag>", "reactions-<sqid>".
std::string channel_id(const ChannelRef &ref);
// The server's channel name and context for query_posts and view events.
const char *server_channel_name(content::ChannelKind kind);

struct ChannelSnapshot {
  content::MakapixEntries entries;  // the index, newest first
  uint32_t cached = 0;              // entries whose file is in the cache
  uint32_t last_refresh = 0;        // epoch seconds, 0 never
  bool refreshing = false;
  std::string error;                // the last refresh failure ("" when fine)
};

// What the application provides: the commands the site can send, and playback.
struct Hooks {
  std::function<void()> next;
  std::function<void()> previous;
  std::function<void(bool paused)> set_paused;
  std::function<void(uint8_t brightness)> set_brightness;
  std::function<void(uint16_t rotation)> set_rotation;
  // A downloaded artwork to play now: its file (absolute path or "mem:<key>"), post id
  // (-1 for a plain URL) and a display name.
  std::function<void(const std::string &path, int32_t post_id, const std::string &name)> play_artwork;
  // A playset from the site (play_channel, play_playset, the Followed built-in).
  std::function<void(const content::Playset &playset)> play_playset;
  std::function<int32_t()> current_post_id;  // -1 when no Makapix artwork is up
  std::function<bool()> is_paused;
};

bool start(const Hooks &hooks);
Status status();
bool paired();

// Pairing (spec 13): asks the server for a code; status() carries it until the owner
// enters it on the site or it expires (15 min).
bool pair(std::string &error);
void cancel_pairing();
bool unpair(std::string &error);

// The show tells the worker which Makapix channels are in play (refreshes and
// downloads target them) and reads their indexes.
void set_active_channels(const std::vector<ChannelRef> &channels);
bool snapshot(const ChannelRef &ref, ChannelSnapshot &out);
// Where an entry's file lives: an absolute card path, or "mem:<key>" without a card.
std::string artwork_path(const content::MakapixEntry &entry);
// Bytes of a "mem:" path (the memory cache used without a card).
bool memory_bytes(const std::string &path, std::vector<uint8_t> &out);
// The show reports what it did with an entry.
void note_load_failed(const content::MakapixEntry &entry, bool missing);
// An artwork went up: presence and, after 5 s on the panel, a view event.
void note_shown(int32_t post_id, const ChannelRef *channel, bool intentional);
void note_hidden();  // nothing of Makapix is up any more

// Play-this: a post by sqid or site URL (resolved and downloaded, then hooks.play_artwork),
// or an arbitrary artwork URL. Both are asynchronous; false when the input is unusable.
bool play_post(const std::string &sqid_or_url, std::string &error);
bool play_url(const std::string &url, std::string &error);
// Like or unlike a post as the owner (blocks up to 20 s; needs pairing).
bool like(int32_t post_id, bool liked, std::string &error);
// The Followed built-in: asks the server for followed_artists and hands the playset to
// hooks.play_playset when it lands (asynchronous). False when not paired.
bool play_followed(std::string &error);

// The cache sweep (spec 5.4): deletes every file under cache/, downloads/ and channels/
// whose mtime (last played, or last refresh for an index) is older than `older_than_s`,
// or implausible (written under a wrong clock), and clears the cached flag of the
// entries whose file went so the download loop fetches them again. Card I/O: seconds
// for thousands of files; runs on the caller's task. False (with `error`) without a
// card, without a synced clock, or while another sweep runs. A dry run only counts.
struct SweepResult {
  bool dry_run = true;
  uint32_t older_than_s = 0;
  uint32_t examined = 0, deleted = 0, indexes_deleted = 0, downloads_deleted = 0;
  uint64_t bytes = 0, freed = 0;
  uint32_t took_ms = 0;
};
bool cache_sweep(uint32_t older_than_s, bool dry_run, SweepResult &out, std::string &error);

}  // namespace p64::makapix

// The Makapix server's document shapes, parsed without any transport (host-tested).
#pragma once

#include <string>

#include "cJSON.h"
#include "p64/content/makapix_index.hpp"
#include "p64/content/playset.hpp"

namespace p64::makapix::contract {

std::string str(const cJSON *obj, const char *key);
double num(const cJSON *obj, const char *key, double fallback = 0);

// Fills an entry from a post object of the feed or the RPC (false for non-artworks, a
// missing id, a storage key that is not a UUID, or an unknown format).
bool entry_from_post(const cJSON *post, content::MakapixEntry &out);

// Appends the artworks of one listing page (`list_key` "posts" or "items") and reads the
// cursor; `has_more` is false whenever the cursor is empty.
void fill_page(const cJSON *root, const char *list_key, content::MakapixEntries &out, std::string &next_cursor,
               bool &has_more);

// The file host URL of an entry: <scheme>://<host>/<shard>/<uuid>.<ext>.
std::string download_url(const content::MakapixEntry &e, const char *vault_host, bool tls);

// --- MQTT (docs/mqtt-api/commands.md, player-status.md) ---------------------------------

// A command from the site, parsed. The device acts on `kind`; for the set_* commands and
// the unknown ones the site expects an acknowledgement (`ack_status` non-empty).
struct Command {
  enum class Kind : uint8_t { Invalid, Next, Back, ShowArtwork, PlayPlayset, SetPaused, SetBrightness, SetRotation, None };
  Kind kind = Kind::Invalid;
  std::string id;    // command_id
  std::string type;  // command_type as sent
  content::MakapixEntry entry = {};  // ShowArtwork
  std::string name;                  // ShowArtwork: a display name
  content::Playset playset;          // PlayPlayset (from play_channel or play_playset)
  bool paused = false;               // SetPaused
  uint8_t brightness = 0;            // SetBrightness
  uint16_t rotation = 0;             // SetRotation
  std::string ack_status;  // "ok", "error", "unsupported", or "" for no acknowledgement
  std::string ack_error;   // with "error"
  bool republish_state = false;  // the retained state changed
  std::string warning;           // a command that cannot be acted on, for the log
};
Command parse_command(const char *json, size_t len);

// The payloads the device publishes.
std::string status_json(const std::string &player_key, int32_t current_post_id, const char *firmware_version);
std::string state_json(bool paused, uint8_t brightness, uint16_t rotation);
std::string capabilities_json(const char *firmware_version);
struct ViewEvent {
  int32_t post_id = 0;
  std::string timestamp;  // ISO 8601 UTC
  bool intentional = false;
  uint8_t play_order = 2;  // 0 server order, 1 created, 2 random
  std::string channel;     // server channel name
  std::string user_sqid, hashtag;
};
std::string view_json(const ViewEvent &v, const std::string &player_key);
// `error` null for none (the site's schema has "error": null).
std::string ack_json(const std::string &command_id, const char *status, const char *error);

}  // namespace p64::makapix::contract

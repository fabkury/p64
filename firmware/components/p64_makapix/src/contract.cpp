// The server's document shapes, as pure code (no ESP-IDF include; host-tested in
// tests/host/unit/makapix.cpp). api.cpp does the transport. Contract:
// reference/makapix/docs/player/querying-artwork.md, docs/http-api/player-rpc.md.
#include "contract.hpp"

#include <cstring>

#include "p64/content/playset_json.hpp"

namespace p64::makapix::contract {

std::string str(const cJSON *obj, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
}

double num(const cJSON *obj, const char *key, double fallback) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return (v && cJSON_IsNumber(v)) ? v->valuedouble : fallback;
}

void fill_page(const cJSON *root, const char *list_key, content::MakapixEntries &out, std::string &next_cursor,
               bool &has_more) {
  const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, list_key);
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, items) {
    content::MakapixEntry e;
    if (entry_from_post(item, e)) out.push_back(e);
  }
  next_cursor = str(root, "next_cursor");
  const cJSON *more = cJSON_GetObjectItemCaseSensitive(root, "has_more");
  has_more = more ? cJSON_IsTrue(more) : !next_cursor.empty();
  if (next_cursor.empty()) has_more = false;
}

bool entry_from_post(const cJSON *post, content::MakapixEntry &out) {
  out = content::MakapixEntry{};
  const std::string kind = str(post, "kind");
  if (!kind.empty() && kind != "artwork") return false;
  double id = num(post, "post_id", -1);
  if (id < 0) id = num(post, "id", -1);
  if (id < 0) return false;
  out.post_id = static_cast<int32_t>(id);
  const std::string key = str(post, "storage_key");
  if (!content::parse_uuid(key.c_str(), out.storage_key)) return false;
  const std::string art_url = str(post, "art_url");
  std::string shard, file;
  content::MakapixFormat format = content::MakapixFormat::Unknown;
  if (!art_url.empty() && content::split_art_url(art_url, shard, file, format)) {
    std::strncpy(out.shard, shard.c_str(), sizeof(out.shard) - 1);
  } else {
    const std::string s = str(post, "storage_shard");
    std::strncpy(out.shard, s.c_str(), sizeof(out.shard) - 1);
  }
  if (format == content::MakapixFormat::Unknown) format = content::makapix_format_from_name(str(post, "native_format"));
  if (format == content::MakapixFormat::Unknown) {
    // The feed lists the files; the native one is the artist's upload.
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(post, "files");
    const cJSON *f = nullptr;
    cJSON_ArrayForEach(f, files) {
      const cJSON *native = cJSON_GetObjectItemCaseSensitive(f, "is_native");
      if (native && cJSON_IsTrue(native)) {
        format = content::makapix_format_from_name(str(f, "format"));
        break;
      }
    }
  }
  if (format == content::MakapixFormat::Unknown) return false;
  out.format = static_cast<uint8_t>(format);
  const std::string sqid = str(post, "public_sqid");
  std::strncpy(out.sqid, sqid.c_str(), sizeof(out.sqid) - 1);
  out.width = static_cast<uint16_t>(num(post, "width"));
  out.height = static_cast<uint16_t>(num(post, "height"));
  out.frame_count = static_cast<uint16_t>(num(post, "frame_count"));
  out.created_at = content::parse_iso8601_utc(str(post, "created_at"));
  out.modified_at = content::parse_iso8601_utc(str(post, "artwork_modified_at"));
  return true;
}

std::string download_url(const content::MakapixEntry &e, const char *vault_host, bool tls) {
  std::string url = tls ? "https://" : "http://";
  url += vault_host;
  url += '/';
  if (e.shard[0]) {
    url += e.shard;
    url += '/';
  }
  url += content::format_uuid(e.storage_key);
  url += content::makapix_format_ext(static_cast<content::MakapixFormat>(e.format));
  return url;
}

// --- MQTT -------------------------------------------------------------------------------

namespace {

std::string print(cJSON *root) {
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text) return "";
  std::string out(text);
  cJSON_free(text);
  return out;
}

bool channel_from_command(const cJSON *payload, content::ChannelSpec &spec) {
  const std::string name = str(payload, "channel_name");
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
    return false;
  }
  return true;
}

}  // namespace

Command parse_command(const char *json, size_t len) {
  Command c;
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!root) {
    c.warning = "command: not JSON";
    return c;
  }
  c.id = str(root, "command_id");
  c.type = str(root, "command_type");
  const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
  const std::string &type = c.type;
  if (type == "swap_next") {
    c.kind = Command::Kind::Next;
  } else if (type == "swap_back") {
    c.kind = Command::Kind::Back;
  } else if (type == "show_artwork") {
    content::MakapixEntry &e = c.entry;
    e.post_id = static_cast<int32_t>(num(payload, "post_id", -1));
    const std::string key = str(payload, "storage_key");
    const std::string shard = str(payload, "storage_shard");
    const content::MakapixFormat format = content::makapix_format_from_name(str(payload, "native_format"));
    if (e.post_id >= 0 && content::parse_uuid(key.c_str(), e.storage_key) && format != content::MakapixFormat::Unknown) {
      std::strncpy(e.shard, shard.c_str(), sizeof(e.shard) - 1);
      e.format = static_cast<uint8_t>(format);
      e.width = static_cast<uint16_t>(num(payload, "width", 0));
      e.height = static_cast<uint16_t>(num(payload, "height", 0));
      c.name = "post " + std::to_string(e.post_id);
      c.kind = Command::Kind::ShowArtwork;
    } else {
      c.kind = Command::Kind::None;
      c.warning = "show_artwork: incomplete payload";
    }
  } else if (type == "play_channel") {
    content::ChannelSpec spec;
    std::string e;
    if (channel_from_command(payload, spec) && spec.validate(e)) {
      c.playset.name = "Makapix";
      c.playset.channels.push_back(spec);
      c.kind = Command::Kind::PlayPlayset;
    } else {
      c.kind = Command::Kind::None;
      c.warning = "play_channel: unusable payload (" + e + ")";
    }
  } else if (type == "play_playset") {
    content::Playset &p = c.playset;
    std::string e;
    if (content::playset_from_json(payload, p, e)) {
      const std::string name = str(payload, "playset_name");
      p.name = name.empty() ? "Makapix" : name;
      content::Builtin b;
      p.builtin = content::builtin_from_name(p.name, b) || p.name == "followed_artists";
      if (p.name == "followed_artists") p.name = content::builtin_name(content::Builtin::Followed);
      // Drop channels this device cannot play yet (URL lists, pinned lists). A p3a's
      // "sdcard" channel is kept: it maps to this device's own card (Local).
      for (auto it = p.channels.begin(); it != p.channels.end();) {
        std::string ce;
        if (!it->supported() || !it->validate(ce)) {
          it = p.channels.erase(it);
        } else {
          ++it;
        }
      }
      if (!p.channels.empty()) {
        c.kind = Command::Kind::PlayPlayset;
      } else {
        c.kind = Command::Kind::None;
        c.warning = "play_playset: no playable channels";
      }
    } else {
      c.kind = Command::Kind::None;
      c.warning = "play_playset: " + e;
    }
  } else if (type == "set_paused") {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(payload, "paused");
    if (v && cJSON_IsBool(v)) {
      c.kind = Command::Kind::SetPaused;
      c.paused = cJSON_IsTrue(v);
      c.ack_status = "ok";
      c.republish_state = true;
    } else {
      c.kind = Command::Kind::None;
      c.ack_status = "error";
      c.ack_error = "missing or invalid 'paused' field";
    }
  } else if (type == "set_brightness") {
    const double v = num(payload, "value", -1);
    if (v >= 1 && v <= 255) {
      c.kind = Command::Kind::SetBrightness;
      c.brightness = static_cast<uint8_t>(v);
      c.ack_status = "ok";
      c.republish_state = true;
    } else {
      c.kind = Command::Kind::None;
      c.ack_status = "error";
      c.ack_error = "brightness must be 1 to 255";
    }
  } else if (type == "set_rotation") {
    const double v = num(payload, "value", -1);
    if (v == 0 || v == 90 || v == 180 || v == 270) {
      c.kind = Command::Kind::SetRotation;
      c.rotation = static_cast<uint16_t>(v);
      c.ack_status = "ok";
      c.republish_state = true;
    } else {
      c.kind = Command::Kind::None;
      c.ack_status = "error";
      c.ack_error = "rotation must be 0, 90, 180, or 270";
    }
  } else if (type == "set_mirror") {
    c.kind = Command::Kind::None;
    c.ack_status = "unsupported";
  } else {
    c.kind = Command::Kind::None;
    c.warning = "command " + type + ": unknown";
    if (!c.id.empty()) c.ack_status = "unsupported";
  }
  cJSON_Delete(root);
  return c;
}

std::string status_json(const std::string &player_key, int32_t current_post_id, const char *firmware_version) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "player_key", player_key.c_str());
  cJSON_AddStringToObject(root, "status", "online");
  if (current_post_id >= 0) cJSON_AddNumberToObject(root, "current_post_id", current_post_id);
  cJSON_AddStringToObject(root, "firmware_version", firmware_version);
  return print(root);
}

std::string state_json(bool paused, uint8_t brightness, uint16_t rotation) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "is_paused", paused);
  cJSON_AddNumberToObject(root, "brightness", brightness);
  cJSON_AddNumberToObject(root, "rotation", rotation);
  return print(root);
}

std::string capabilities_json(const char *firmware_version) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "firmware_version", firmware_version);
  cJSON *features = cJSON_AddObjectToObject(root, "features");
  cJSON_AddItemToObject(features, "pause", cJSON_CreateObject());
  cJSON *brightness = cJSON_AddObjectToObject(features, "brightness");
  cJSON_AddNumberToObject(brightness, "min", 1);
  cJSON_AddNumberToObject(brightness, "max", 255);
  cJSON_AddNumberToObject(brightness, "step", 1);
  cJSON *rotation = cJSON_AddObjectToObject(features, "rotation");
  cJSON *values = cJSON_AddArrayToObject(rotation, "values");
  for (int v : {0, 90, 180, 270}) cJSON_AddItemToArray(values, cJSON_CreateNumber(v));
  return print(root);
}

std::string view_json(const ViewEvent &v, const std::string &player_key) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "post_id", v.post_id);
  cJSON_AddStringToObject(root, "timestamp", v.timestamp.c_str());
  cJSON_AddStringToObject(root, "timezone", "");
  cJSON_AddStringToObject(root, "intent", v.intentional ? "artwork" : "channel");
  cJSON_AddNumberToObject(root, "play_order", v.play_order);
  cJSON_AddStringToObject(root, "channel", v.channel.c_str());
  cJSON_AddStringToObject(root, "player_key", player_key.c_str());
  if (!v.user_sqid.empty()) cJSON_AddStringToObject(root, "channel_user_sqid", v.user_sqid.c_str());
  if (!v.hashtag.empty()) cJSON_AddStringToObject(root, "channel_hashtag", v.hashtag.c_str());
  cJSON_AddBoolToObject(root, "request_ack", false);
  return print(root);
}

std::string ack_json(const std::string &command_id, const char *status, const char *error) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "command_id", command_id.c_str());
  cJSON_AddStringToObject(root, "status", status);
  if (error) {
    cJSON_AddStringToObject(root, "error", error);
  } else {
    cJSON_AddNullToObject(root, "error");
  }
  return print(root);
}

}  // namespace p64::makapix::contract

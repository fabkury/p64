#include "api.hpp"

#include <cstring>

#include "contract.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "p64/net/fetch.hpp"
#include "sdkconfig.h"

namespace p64::makapix::api {
namespace {

constexpr const char *TAG = "makapix";
constexpr size_t kPageLimit = 50;
constexpr size_t kMaxListingBytes = 160 * 1024;

using contract::num;
using contract::str;

// Parses a JSON reply; false with the server's detail when the status is not 2xx.
cJSON *parse_reply(const net::fetch::Result &r, const char *what, std::string &error) {
  if (r.error != ESP_OK && r.status == 0) {
    error = std::string(what) + ": " + esp_err_to_name(r.error);
    return nullptr;
  }
  cJSON *json = r.body.empty() ? nullptr : cJSON_ParseWithLength(reinterpret_cast<const char *>(r.body.data()), r.body.size());
  if (r.status < 200 || r.status >= 300) {
    std::string detail;
    if (json) {
      detail = str(json, "detail");
      if (detail.empty()) detail = str(json, "error");
      if (detail.empty()) {
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(json, "detail");
        if (d && cJSON_IsArray(d) && cJSON_GetArraySize(d) > 0) detail = str(cJSON_GetArrayItem(d, 0), "msg");
      }
      cJSON_Delete(json);
    }
    error = std::string(what) + ": HTTP " + std::to_string(r.status) + (detail.empty() ? "" : " " + detail);
    return nullptr;
  }
  if (!json) {
    error = std::string(what) + ": not JSON";
    return nullptr;
  }
  return json;
}

bool request_json(const char *method, const std::string &url, const std::string &token, cJSON *body_json,
                  size_t max_bytes, net::fetch::Result &result, const char *what, std::string &error, cJSON **reply,
                  net::fetch::Session *session = nullptr) {
  net::fetch::Request req;
  req.url = url;
  req.method = method;
  req.max_bytes = max_bytes;
  if (!token.empty()) req.headers.emplace_back("Authorization", "Bearer " + token);
  req.headers.emplace_back("Accept", "application/json");
  if (body_json) {
    char *text = cJSON_PrintUnformatted(body_json);
    if (!text) {
      error = "out of memory";
      return false;
    }
    req.body = text;
    cJSON_free(text);
  }
  if (session) {
    session->perform(req, result);
  } else {
    net::fetch::perform(req, result);
  }
  *reply = parse_reply(result, what, error);
  return *reply != nullptr;
}

}  // namespace

std::string base_url() { return std::string("https://") + CONFIG_P64_MAKAPIX_HOST + "/api"; }

bool provision(Provision &out, std::string &error) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "device_model", "p64");
  const esp_app_desc_t *app = esp_app_get_description();
  cJSON_AddStringToObject(body, "firmware_version", app ? app->version : "0");
  net::fetch::Result r;
  cJSON *reply = nullptr;
  const bool ok = request_json("POST", base_url() + "/player/provision", "", body, 16 * 1024, r, "provision", error, &reply);
  cJSON_Delete(body);
  if (!ok) return false;
  out.player_key = str(reply, "player_key");
  out.code = str(reply, "registration_code");
  out.expires_at = str(reply, "registration_code_expires_at");
  const cJSON *broker = cJSON_GetObjectItemCaseSensitive(reply, "mqtt_broker");
  if (broker) {
    out.mqtt_host = str(broker, "host");
    out.mqtt_port = static_cast<uint16_t>(num(broker, "port"));
  }
  const cJSON *https = cJSON_GetObjectItemCaseSensitive(reply, "https_api");
  if (https) out.https_base = str(https, "base_url");
  cJSON_Delete(reply);
  if (out.player_key.empty() || out.code.empty()) {
    error = "provision: reply without key or code";
    return false;
  }
  return true;
}

int credentials(const std::string &player_key, creds::Credentials &out, std::string &error) {
  net::fetch::Request req;
  req.url = base_url() + "/player/" + player_key + "/credentials";
  req.max_bytes = 64 * 1024;
  req.headers.emplace_back("Accept", "application/json");
  net::fetch::Result r;
  net::fetch::perform(req, r);
  if (r.status == 404) return 0;
  cJSON *reply = parse_reply(r, "credentials", error);
  if (!reply) return -1;
  out.player_key = player_key;
  out.ca_pem = str(reply, "ca_pem");
  out.cert_pem = str(reply, "cert_pem");
  out.key_pem = str(reply, "key_pem");
  out.api_token = str(reply, "api_token");
  const cJSON *broker = cJSON_GetObjectItemCaseSensitive(reply, "broker");
  if (broker) {
    out.mqtt_host = str(broker, "host");
    out.mqtt_port = static_cast<uint16_t>(num(broker, "port"));
  }
  const cJSON *https = cJSON_GetObjectItemCaseSensitive(reply, "https_api");
  if (https) out.https_base = str(https, "base_url");
  cJSON_Delete(reply);
  if (!out.complete()) {
    error = "credentials: reply without the PEMs";
    return -1;
  }
  return 1;
}

bool promoted_page(const std::string &cursor, content::MakapixEntries &out, std::string &next_cursor, bool &has_more,
                   std::string &error, net::fetch::Session *session) {
  std::string url = base_url() +
                    "/feed/promoted?fields=id,public_sqid,storage_key,art_url,files,width,height,frame_count,created_at,"
                    "artwork_modified_at,kind&limit=" +
                    std::to_string(kPageLimit);
  if (!cursor.empty()) url += "&cursor=" + cursor;
  net::fetch::Result r;
  cJSON *reply = nullptr;
  if (!request_json("GET", url, "", nullptr, kMaxListingBytes, r, "promoted feed", error, &reply, session)) return false;
  contract::fill_page(reply, "items", out, next_cursor, has_more);
  cJSON_Delete(reply);
  return true;
}

const char *server_channel(const ChannelRef &ref) { return server_channel_name(ref.kind); }

bool query_page(const std::string &token, const ChannelRef &ref, uint16_t max_side, const std::string &cursor,
                content::MakapixEntries &out, std::string &next_cursor, bool &has_more, std::string &error,
                net::fetch::Session *session) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "request_type", "query_posts");
  cJSON_AddStringToObject(body, "channel", server_channel(ref));
  if (ref.kind == content::ChannelKind::MakapixArtist || ref.kind == content::ChannelKind::MakapixReactions) {
    cJSON_AddStringToObject(body, "user_sqid", ref.identifier.c_str());
  } else if (ref.kind == content::ChannelKind::MakapixHashtag) {
    cJSON_AddStringToObject(body, "hashtag", ref.identifier.c_str());
  }
  cJSON_AddStringToObject(body, "sort", ref.kind == content::ChannelKind::MakapixReactions ? "reacted_at" : "server_order");
  if (cursor.empty()) {
    cJSON_AddNullToObject(body, "cursor");
  } else {
    cJSON_AddStringToObject(body, "cursor", cursor.c_str());
  }
  cJSON_AddNumberToObject(body, "limit", kPageLimit);
  cJSON *fields = cJSON_AddArrayToObject(body, "include_fields");
  for (const char *f : {"width", "height", "frame_count", "artwork_modified_at"}) cJSON_AddItemToArray(fields, cJSON_CreateString(f));
  // AMP criteria (docs/player/querying-artwork.md): only artworks that fit the size limit.
  cJSON *criteria = cJSON_AddArrayToObject(body, "criteria");
  for (const char *side : {"width", "height"}) {
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "field", side);
    cJSON_AddStringToObject(c, "op", "lte");
    cJSON_AddNumberToObject(c, "value", max_side);
    cJSON_AddItemToArray(criteria, c);
  }
  net::fetch::Result r;
  cJSON *reply = nullptr;
  const bool ok = request_json("POST", base_url() + "/player/rpc", token, body, kMaxListingBytes, r, "query_posts", error,
                               &reply, session);
  cJSON_Delete(body);
  if (!ok) return false;
  const cJSON *success = cJSON_GetObjectItemCaseSensitive(reply, "success");
  if (success && !cJSON_IsTrue(success)) {
    error = "query_posts: " + str(reply, "error") + " (" + str(reply, "error_code") + ")";
    cJSON_Delete(reply);
    return false;
  }
  contract::fill_page(reply, "posts", out, next_cursor, has_more);
  cJSON_Delete(reply);
  return true;
}

bool post_by_sqid(const std::string &sqid, content::MakapixEntry &out, std::string &title, std::string &error) {
  net::fetch::Result r;
  cJSON *reply = nullptr;
  if (!request_json("GET", base_url() + "/player/p/" + sqid, "", nullptr, 64 * 1024, r, "post lookup", error, &reply)) return false;
  const bool ok = entry_from_post(reply, out);
  title = str(reply, "title");
  cJSON_Delete(reply);
  if (!ok) error = "post lookup: not an artwork";
  return ok;
}

bool view(const std::string &token, const ViewEvent &v, std::string &error) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "post_id", v.post_id);
  cJSON_AddStringToObject(body, "timestamp", v.timestamp.c_str());
  cJSON_AddStringToObject(body, "timezone", "");
  cJSON_AddStringToObject(body, "intent", v.intentional ? "artwork" : "channel");
  cJSON_AddNumberToObject(body, "play_order", v.play_order);
  cJSON_AddStringToObject(body, "channel", v.channel.c_str());
  if (!v.user_sqid.empty()) cJSON_AddStringToObject(body, "channel_user_sqid", v.user_sqid.c_str());
  if (!v.hashtag.empty()) cJSON_AddStringToObject(body, "channel_hashtag", v.hashtag.c_str());
  net::fetch::Result r;
  cJSON *reply = nullptr;
  const bool ok = request_json("POST", base_url() + "/player/events/view", token, body, 8 * 1024, r, "view", error, &reply);
  cJSON_Delete(body);
  if (reply) cJSON_Delete(reply);
  return ok;
}

bool reaction(const std::string &token, int32_t post_id, bool add, std::string &error) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "request_type", add ? "submit_reaction" : "revoke_reaction");
  cJSON_AddNumberToObject(body, "post_id", post_id);
  cJSON_AddStringToObject(body, "emoji", "\xE2\x9D\xA4\xEF\xB8\x8F");  // the red heart
  net::fetch::Result r;
  cJSON *reply = nullptr;
  const bool ok = request_json("POST", base_url() + "/player/rpc", token, body, 8 * 1024, r, "reaction", error, &reply);
  cJSON_Delete(body);
  if (!ok) return false;
  const cJSON *success = cJSON_GetObjectItemCaseSensitive(reply, "success");
  const bool fine = success && cJSON_IsTrue(success);
  if (!fine) error = "reaction: " + str(reply, "error");
  cJSON_Delete(reply);
  return fine;
}

bool get_playset(const std::string &token, const std::string &name, content::Playset &out, std::string &error) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "request_type", "get_playset");
  cJSON_AddStringToObject(body, "playset_name", name.c_str());
  net::fetch::Result r;
  cJSON *reply = nullptr;
  const bool ok = request_json("POST", base_url() + "/player/rpc", token, body, 32 * 1024, r, "get_playset", error, &reply);
  cJSON_Delete(body);
  if (!ok) return false;
  const cJSON *success = cJSON_GetObjectItemCaseSensitive(reply, "success");
  if (success && !cJSON_IsTrue(success)) {
    error = "get_playset: " + str(reply, "error");
    cJSON_Delete(reply);
    return false;
  }
  out = content::Playset{};
  const cJSON *channels = cJSON_GetObjectItemCaseSensitive(reply, "channels");
  const cJSON *ch = nullptr;
  cJSON_ArrayForEach(ch, channels) {
    content::ChannelSpec spec;
    if (!content::kind_from_name(str(ch, "type"), str(ch, "name"), spec.kind)) continue;
    spec.identifier = str(ch, "identifier");
    spec.display_name = str(ch, "display_name");
    spec.weight = static_cast<uint32_t>(num(ch, "weight"));
    std::string e;
    if (spec.validate(e) && out.channels.size() < content::kMaxChannels) out.channels.push_back(spec);
  }
  cJSON_Delete(reply);
  return true;
}

bool renew_cert(const std::string &token, creds::Credentials &io, uint32_t &expires_at, std::string &error) {
  net::fetch::Result r;
  cJSON *reply = nullptr;
  if (!request_json("POST", base_url() + "/player/renew-cert", token, nullptr, 64 * 1024, r, "renew-cert", error, &reply)) {
    return false;
  }
  const std::string cert = str(reply, "cert_pem"), key = str(reply, "key_pem"), ca = str(reply, "ca_pem");
  expires_at = content::parse_iso8601_utc(str(reply, "cert_expires_at"));
  cJSON_Delete(reply);
  if (cert.empty() || key.empty()) {
    error = "renew-cert: reply without the PEMs";
    return false;
  }
  io.cert_pem = cert;
  io.key_pem = key;
  if (!ca.empty()) io.ca_pem = ca;
  return true;
}

bool rotate_token(const std::string &player_key, std::string &token, std::string &error) {
  net::fetch::Result r;
  cJSON *reply = nullptr;
  if (!request_json("POST", base_url() + "/player/" + player_key + "/token/rotate", "", nullptr, 8 * 1024, r, "token rotate",
                    error, &reply)) {
    return false;
  }
  token = str(reply, "api_token");
  cJSON_Delete(reply);
  if (token.empty()) {
    error = "token rotate: no token in the reply";
    return false;
  }
  return true;
}

std::string download_url(const content::MakapixEntry &e) {
#ifdef CONFIG_P64_MAKAPIX_VAULT_TLS
  return contract::download_url(e, CONFIG_P64_MAKAPIX_VAULT_HOST, true);
#else
  return contract::download_url(e, CONFIG_P64_MAKAPIX_VAULT_HOST, false);
#endif
}

bool entry_from_post(const cJSON *post, content::MakapixEntry &out) { return contract::entry_from_post(post, out); }

bool download(const std::string &url, std::vector<uint8_t> &out, size_t max_bytes, bool &missing, std::string &error,
              net::fetch::Session *session) {
  missing = false;
  net::fetch::Request req;
  req.url = url;
  req.max_bytes = max_bytes;
  req.timeout_ms = 30000;
  net::fetch::Result r;
  if (session) {
    session->perform(req, r);
  } else {
    net::fetch::perform(req, r);
  }
  if (r.status == 404 || r.status == 410) {
    missing = true;
    error = "not on the server (HTTP " + std::to_string(r.status) + ")";
    return false;
  }
  if (r.status == 0) {
    error = std::string("download: ") + esp_err_to_name(r.error);
    return false;
  }
  if (r.status < 200 || r.status >= 300) {
    error = "download: HTTP " + std::to_string(r.status);
    return false;
  }
  if (r.error != ESP_OK) {
    error = std::string("download: ") + esp_err_to_name(r.error);
    return false;
  }
  if (r.body.size() < 12) {
    error = "download: empty file";
    return false;
  }
  out = std::move(r.body);
  ESP_LOGD(TAG, "downloaded %u bytes in %u ms from %s", static_cast<unsigned>(out.size()), r.took_ms, url.c_str());
  return true;
}

}  // namespace p64::makapix::api

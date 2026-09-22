// The server's document shapes, as pure code (no ESP-IDF include; host-tested in
// tests/host/unit/makapix.cpp). api.cpp does the transport. Contract:
// reference/makapix/docs/player/querying-artwork.md, docs/http-api/player-rpc.md.
#include "contract.hpp"

#include <cstring>

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

}  // namespace p64::makapix::contract

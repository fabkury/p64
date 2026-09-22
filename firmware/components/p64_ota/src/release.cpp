#include "release.hpp"

#include <cctype>
#include <cstring>

#include "cJSON.h"

namespace p64::ota::release {

bool parse(const char *json, size_t len, const std::string &asset, size_t notes_max, Release &out,
           std::string &error) {
  out = Release{};
  cJSON *root = cJSON_ParseWithLength(json, len);
  if (!root) {
    error = "release JSON unreadable";
    return false;
  }
  const cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
  const cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
  const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
  out.version = tag && cJSON_IsString(tag) ? tag->valuestring : "";
  if (!out.version.empty() && (out.version[0] == 'v' || out.version[0] == 'V')) out.version.erase(0, 1);
  out.notes = body && cJSON_IsString(body) ? body->valuestring : "";
  if (out.notes.size() > notes_max) out.notes = out.notes.substr(0, notes_max) + "...";
  const std::string sha_name = asset + ".sha256";
  const cJSON *a;
  cJSON_ArrayForEach(a, assets) {
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(a, "name");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url");
    const cJSON *sz = cJSON_GetObjectItemCaseSensitive(a, "size");
    if (!name || !cJSON_IsString(name) || !url || !cJSON_IsString(url)) continue;
    if (asset == name->valuestring) {
      out.bin_url = url->valuestring;
      if (sz && cJSON_IsNumber(sz)) out.size = static_cast<uint32_t>(sz->valuedouble);
    } else if (sha_name == name->valuestring) {
      out.sha_url = url->valuestring;
    }
  }
  cJSON_Delete(root);
  if (out.version.empty()) {
    error = "release without a tag";
    return false;
  }
  return true;
}

bool hex_to_bin(const std::string &hex, uint8_t out[32]) {
  if (hex.size() < 64) return false;
  for (int i = 0; i < 32; ++i) {
    unsigned v = 0;
    for (int k = 0; k < 2; ++k) {
      const char c = hex[i * 2 + k];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return false;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

bool digest_from_checksum_file(const std::string &text, uint8_t out[32]) {
  size_t start = 0;
  while (start < text.size() && !std::isxdigit(static_cast<unsigned char>(text[start]))) ++start;
  return hex_to_bin(text.substr(start), out);
}

}  // namespace p64::ota::release

#include "p64/content/makapix_index.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>

namespace p64::content {
namespace {

constexpr char kMagic[4] = {'P', '6', '4', 'X'};
constexpr uint16_t kVersion = 1;

struct Header {
  char magic[4];
  uint16_t version;
  uint16_t reserved;
  uint32_t count;
  uint32_t crc;
};
static_assert(sizeof(Header) == 16, "index header");

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

uint32_t crc32_of(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool parse_uuid(const char *text, uint8_t out[16]) {
  if (!text) return false;
  int n = 0;
  int high = -1;
  for (const char *p = text; *p; ++p) {
    if (*p == '-') continue;
    const int v = hex_value(*p);
    if (v < 0) return false;
    if (high < 0) {
      high = v;
    } else {
      if (n >= 16) return false;
      out[n++] = static_cast<uint8_t>((high << 4) | v);
      high = -1;
    }
  }
  return n == 16 && high < 0;
}

std::string format_uuid(const uint8_t bytes[16]) {
  char buf[37];
  std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return buf;
}

MakapixFormat makapix_format_from_name(const std::string &name) {
  if (name == "png") return MakapixFormat::Png;
  if (name == "gif") return MakapixFormat::Gif;
  if (name == "webp") return MakapixFormat::WebP;
  if (name == "bmp") return MakapixFormat::Bmp;
  return MakapixFormat::Unknown;
}

const char *makapix_format_ext(MakapixFormat f) {
  switch (f) {
    case MakapixFormat::Png: return ".png";
    case MakapixFormat::Gif: return ".gif";
    case MakapixFormat::WebP: return ".webp";
    case MakapixFormat::Bmp: return ".bmp";
    default: return ".bin";
  }
}

bool split_art_url(const std::string &art_url, std::string &shard, std::string &file_name, MakapixFormat &format) {
  // scheme://host/<shard...>/<file>
  size_t start = art_url.find("://");
  start = start == std::string::npos ? 0 : start + 3;
  const size_t path = art_url.find('/', start);
  if (path == std::string::npos) return false;
  const size_t last = art_url.rfind('/');
  if (last == std::string::npos || last <= path) return false;
  shard = art_url.substr(path + 1, last - path - 1);
  file_name = art_url.substr(last + 1);
  const size_t q = file_name.find('?');
  if (q != std::string::npos) file_name.resize(q);
  const size_t dot = file_name.rfind('.');
  format = dot == std::string::npos ? MakapixFormat::Unknown : makapix_format_from_name(file_name.substr(dot + 1));
  return !file_name.empty();
}

std::string cache_relative_path(const MakapixEntry &e) {
  const std::string uuid = format_uuid(e.storage_key);
  return "cache/" + uuid.substr(0, 2) + "/" + uuid + makapix_format_ext(static_cast<MakapixFormat>(e.format));
}

uint32_t parse_iso8601_utc(const std::string &s) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  double sec = 0;
  if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  int64_t t = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 +
              static_cast<int64_t>(sec);
  // An explicit offset (+HH:MM / -HH:MM) after the seconds; "Z" or nothing means UTC.
  const size_t tpos = s.find('T');
  const size_t plus = s.find_first_of("+-", tpos == std::string::npos ? 0 : tpos);
  if (plus != std::string::npos) {
    int oh = 0, om = 0;
    if (std::sscanf(s.c_str() + plus + 1, "%d:%d", &oh, &om) >= 1) {
      const int64_t off = oh * 3600 + om * 60;
      t += s[plus] == '+' ? -off : off;
    }
  }
  return t > 0 ? static_cast<uint32_t>(t) : 0;
}

std::vector<uint8_t> serialize_index(const MakapixEntries &entries) {
  std::vector<uint8_t> out(sizeof(Header) + entries.size() * sizeof(MakapixEntry));
  Header h = {};
  std::memcpy(h.magic, kMagic, 4);
  h.version = kVersion;
  h.count = static_cast<uint32_t>(entries.size());
  if (!entries.empty()) std::memcpy(out.data() + sizeof(Header), entries.data(), entries.size() * sizeof(MakapixEntry));
  h.crc = crc32_of(out.data() + sizeof(Header), entries.size() * sizeof(MakapixEntry));
  std::memcpy(out.data(), &h, sizeof(Header));
  return out;
}

bool deserialize_index(const uint8_t *data, size_t len, MakapixEntries &out, std::string &error) {
  out.clear();
  if (len < sizeof(Header)) {
    error = "too short";
    return false;
  }
  Header h;
  std::memcpy(&h, data, sizeof(Header));
  if (std::memcmp(h.magic, kMagic, 4) != 0) {
    error = "not a p64 index";
    return false;
  }
  if (h.version != kVersion) {
    error = "index version " + std::to_string(h.version);
    return false;
  }
  const size_t bytes = static_cast<size_t>(h.count) * sizeof(MakapixEntry);
  if (len != sizeof(Header) + bytes) {
    error = "length mismatch";
    return false;
  }
  if (crc32_of(data + sizeof(Header), bytes) != h.crc) {
    error = "checksum mismatch";
    return false;
  }
  out.resize(h.count);
  if (h.count) std::memcpy(out.data(), data + sizeof(Header), bytes);
  return true;
}

size_t merge_index(const MakapixEntries &previous, MakapixEntries &fresh) {
  std::unordered_map<int32_t, size_t> by_id;
  by_id.reserve(previous.size());
  for (size_t i = 0; i < previous.size(); ++i) by_id[previous[i].post_id] = i;
  size_t kept = 0;
  for (MakapixEntry &e : fresh) {
    auto it = by_id.find(e.post_id);
    if (it == by_id.end()) continue;
    const MakapixEntry &old = previous[it->second];
    const bool same_file = old.modified_at == e.modified_at && std::memcmp(old.storage_key, e.storage_key, 16) == 0 &&
                           old.format == e.format;
    if (same_file) {
      e.flags = old.flags;
      if (!e.sqid[0]) std::memcpy(e.sqid, old.sqid, sizeof(e.sqid));
      if (!e.shard[0]) std::memcpy(e.shard, old.shard, sizeof(e.shard));
      if (!e.width) e.width = old.width;
      if (!e.height) e.height = old.height;
      if (!e.frame_count) e.frame_count = old.frame_count;
    }
    ++kept;
  }
  return previous.size() - kept;
}

}  // namespace p64::content

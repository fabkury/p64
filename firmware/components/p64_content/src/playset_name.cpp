#include "p64/content/playset_name.hpp"

#include <cstdio>

#include "p64/content/playset.hpp"

namespace p64::content {
namespace {

bool ascii_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Spaces and invisibles outside ASCII that would make two names look the same.
bool invisible(uint32_t cp) {
  return cp == 0x00a0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200d) || cp == 0x2028 || cp == 0x2029 ||
         cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff;
}

// Decodes one UTF-8 sequence at s[i]; false on a malformed one.
bool decode(const std::string &s, size_t i, uint32_t &cp, size_t &n) {
  const auto c = static_cast<unsigned char>(s[i]);
  n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
  if (n == 0 || i + n > s.size()) return false;
  const uint32_t floor = n == 2 ? 0x80 : n == 3 ? 0x800 : 0x10000;
  cp = n == 1 ? c : n == 2 ? (c & 0x1fu) : n == 3 ? (c & 0x0fu) : (c & 0x07u);
  for (size_t k = 1; k < n; ++k) {
    const auto b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xc0) != 0x80) return false;
    cp = (cp << 6) | (b & 0x3fu);
  }
  if (n > 1 && (cp < floor || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))) return false;
  return true;
}

}  // namespace

bool legacy_playset_name(const std::string &name) {
  if (name.empty() || name.size() > kMaxPlaysetName) return false;
  for (char c : name) {
    if (!ascii_alnum(c) && c != '_') return false;
  }
  return true;
}

bool valid_playset_name(const std::string &name) {
  if (name.empty() || name.front() == ' ' || name.back() == ' ') return false;
  size_t count = 0;
  for (size_t i = 0; i < name.size();) {
    uint32_t cp = 0;
    size_t n = 0;
    if (!decode(name, i, cp, n)) return false;
    if (cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp <= 0x9f) || invisible(cp)) return false;
    if (++count > kMaxPlaysetName) return false;
    i += n;
  }
  return true;
}

size_t utf8_length(const std::string &s) {
  size_t count = 0;
  for (char c : s) {
    if ((static_cast<unsigned char>(c) & 0xc0) != 0x80) ++count;
  }
  return count;
}

std::string canonical_playset_name(const std::string &name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (c == ' ') {
      if (out.empty() || out.back() == ' ') continue;
      out += ' ';
    } else {
      out += ascii_lower(c);
    }
  }
  if (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool same_playset_name(const std::string &a, const std::string &b) {
  return canonical_playset_name(a) == canonical_playset_name(b);
}

std::string playset_slug(const std::string &name) {
  constexpr size_t kMaxSlug = 24;
  std::string out;
  for (char c : canonical_playset_name(name)) {
    if (ascii_alnum(c)) {
      out += c;
    } else if (!out.empty() && out.back() != '_') {
      out += '_';
    }
    if (out.size() >= kMaxSlug) break;
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out.empty() ? "playset" : out;
}

uint32_t playset_hash(const std::string &name) {
  uint32_t h = 2166136261u;
  for (char c : canonical_playset_name(name)) {
    h ^= static_cast<unsigned char>(c);
    h *= 16777619u;
  }
  return h;
}

std::string playset_file_stem(const std::string &name) {
  if (legacy_playset_name(name)) return name;
  char hex[8];
  std::snprintf(hex, sizeof hex, "%04x", static_cast<unsigned>(playset_hash(name) & 0xffffu));
  return playset_slug(name) + "~" + hex;
}

bool new_style_stem(const std::string &stem) { return stem.find('~') != std::string::npos; }

}  // namespace p64::content

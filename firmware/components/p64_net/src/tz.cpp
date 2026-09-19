#include "p64/net/tz.hpp"

#include <cstring>

namespace p64::net::tz {
namespace {

struct Zone {
  const char *name;
  const char *posix;
};

const Zone kZones[] = {
#include "tz_table.inc"
};

constexpr size_t kCount = sizeof(kZones) / sizeof(kZones[0]);

}  // namespace

const char *posix_for(const std::string &iana_name) {
  // Sorted by name in the generated file: binary search.
  size_t lo = 0, hi = kCount;
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = std::strcmp(kZones[mid].name, iana_name.c_str());
    if (c == 0) return kZones[mid].posix;
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // A POSIX rule passed directly (contains a digit and no slash) is accepted as is.
  bool digit = false;
  for (char ch : iana_name) digit |= (ch >= '0' && ch <= '9');
  if (digit && iana_name.find('/') == std::string::npos && iana_name.size() < 64) {
    static char passthrough[64];
    std::strncpy(passthrough, iana_name.c_str(), sizeof(passthrough) - 1);
    passthrough[sizeof(passthrough) - 1] = 0;
    return passthrough;
  }
  return nullptr;
}

size_t count() { return kCount; }

const char *name_at(size_t index) { return index < kCount ? kZones[index].name : nullptr; }

}  // namespace p64::net::tz

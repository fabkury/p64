#include "version.hpp"

#include <cstdlib>

namespace p64::ota::version {

Parsed parse(const std::string &text) {
  Parsed p;
  size_t i = 0;
  if (i < text.size() && (text[i] == 'v' || text[i] == 'V')) ++i;
  int parts[3] = {0, 0, 0};
  for (int n = 0; n < 3; ++n) {
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return p;
    int v = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      v = v * 10 + (text[i] - '0');
      ++i;
    }
    parts[n] = v;
    if (n < 2) {
      if (i >= text.size() || text[i] != '.') return p;
      ++i;
    }
  }
  p.major = parts[0];
  p.minor = parts[1];
  p.patch = parts[2];
  if (i < text.size()) {
    if (text[i] != '-' && text[i] != '+') return p;
    p.suffix = text.substr(i + 1);
  }
  p.valid = true;
  return p;
}

int compare(const std::string &a, const std::string &b) {
  const Parsed pa = parse(a), pb = parse(b);
  if (!pa.valid || !pb.valid) {
    if (pa.valid != pb.valid) return pa.valid ? 1 : -1;
    return a < b ? -1 : a > b ? 1 : 0;
  }
  if (pa.major != pb.major) return pa.major < pb.major ? -1 : 1;
  if (pa.minor != pb.minor) return pa.minor < pb.minor ? -1 : 1;
  if (pa.patch != pb.patch) return pa.patch < pb.patch ? -1 : 1;
  // Same numbers: a release beats a suffixed build; two suffixes compare by text.
  if (pa.suffix.empty() != pb.suffix.empty()) return pa.suffix.empty() ? 1 : -1;
  return pa.suffix < pb.suffix ? -1 : pa.suffix > pb.suffix ? 1 : 0;
}

bool is_newer(const std::string &candidate, const std::string &running) { return compare(candidate, running) > 0; }

}  // namespace p64::ota::version

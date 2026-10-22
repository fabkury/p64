// p64 -- firmware version strings "MAJOR.MINOR.PATCH[-suffix][+build]" (an optional leading
// "v"), pure and host-tested. A suffixed build (0.1.0-dev) is older than the same numbers
// without one (0.1.0). Build metadata after "+" (0.2.1+private, the private build of the
// public 0.2.1) names the same version and never affects the ordering, as in semver.
#pragma once

#include <string>

namespace p64::ota::version {

struct Parsed {
  int major = 0, minor = 0, patch = 0;
  std::string suffix;  // after "-", the pre-release label
  std::string build;   // after "+", build metadata (ignored by compare)
  bool valid = false;
};

Parsed parse(const std::string &text);
// -1 when a < b, 0 when equal, +1 when a > b. Two invalid versions compare by string.
int compare(const std::string &a, const std::string &b);
// True when `candidate` is newer than `running`.
bool is_newer(const std::string &candidate, const std::string &running);

}  // namespace p64::ota::version

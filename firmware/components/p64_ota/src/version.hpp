// p64 -- firmware version strings "MAJOR.MINOR.PATCH[-suffix]" (an optional leading "v"),
// pure and host-tested. A suffixed build (0.1.0-dev) is older than the same numbers
// without one (0.1.0).
#pragma once

#include <string>

namespace p64::ota::version {

struct Parsed {
  int major = 0, minor = 0, patch = 0;
  std::string suffix;
  bool valid = false;
};

Parsed parse(const std::string &text);
// -1 when a < b, 0 when equal, +1 when a > b. Two invalid versions compare by string.
int compare(const std::string &a, const std::string &b);
// True when `candidate` is newer than `running`.
bool is_newer(const std::string &candidate, const std::string &running);

}  // namespace p64::ota::version

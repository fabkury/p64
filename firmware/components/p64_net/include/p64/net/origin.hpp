// p64 -- whether a request comes from this device's own pages. Pure (no ESP-IDF include),
// host-tested.
//
// A page on another site can make its visitor's browser send requests to a device on the
// visitor's network. The browser says where the page came from in the Origin header,
// which scripts and tools do not send. The HTTP server's gate refuses every request that
// changes the device, and the WebSocket, when the Origin is not the device itself
// (decision D29 of firmware/docs/testing-strategy/decisions.md, ADR 0019).
#pragma once

#include <cstddef>
#include <string>

namespace p64::net::http {

// `origin` is the Origin header ("" when absent), `host` the Host header. True when
// there is no Origin (not a browser's cross-site request) or when its authority (what
// follows "scheme://") equals the Host, letter case aside. "null" (a sandboxed page, a
// local file) and anything else is foreign.
inline bool same_origin(const std::string &origin, const std::string &host) {
  if (origin.empty()) return true;
  const size_t scheme = origin.find("://");
  if (scheme == std::string::npos) return false;
  const std::string authority = origin.substr(scheme + 3);
  if (host.empty() || authority.size() != host.size()) return false;
  for (size_t i = 0; i < host.size(); ++i) {
    const char a = authority[i], b = host[i];
    const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
    const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
    if (la != lb) return false;
  }
  return true;
}

}  // namespace p64::net::http

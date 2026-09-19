// The pairing credentials in their own NVS namespace ("makapix"), as the spec's storage
// section asks. PEMs are blobs; the rest strings.
#pragma once

#include <cstdint>
#include <string>

namespace p64::makapix::creds {

struct Credentials {
  std::string player_key;
  std::string ca_pem, cert_pem, key_pem;
  std::string api_token;
  std::string mqtt_host;
  uint16_t mqtt_port = 0;
  std::string https_base;  // "https://makapix.club/api"
  bool complete() const { return !player_key.empty() && !cert_pem.empty() && !key_pem.empty() && !ca_pem.empty(); }
};

bool load(Credentials &out);  // true when a player key is stored
bool save(const Credentials &c);
bool save_pems(const std::string &ca, const std::string &cert, const std::string &key);
bool save_token(const std::string &token);
bool erase();

}  // namespace p64::makapix::creds

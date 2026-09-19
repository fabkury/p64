// p64 -- firmware updates (spec 15.2): the latest GitHub release of the configured
// repository is checked every 12 h (when the setting allows) and on request; the user
// installs from the Update page; the image is written to the other slot, its SHA256
// checked against the release's `.sha256` asset, then the slot is made bootable and the
// user reboots. An install from any URL (a local build) is also possible, with an
// optional SHA256. Rollback boots the other slot's image.
#pragma once

#include <cstdint>
#include <string>

#include "cJSON.h"

namespace p64::ota {

enum class State : uint8_t { Idle, Checking, UpToDate, Available, Downloading, Verifying, ReadyToReboot, Error };

struct Status {
  State state = State::Idle;
  std::string current_version;
  std::string available_version;  // the latest release's tag (without "v") when known
  std::string notes;              // release notes, truncated
  std::string download_url, sha256_url;
  uint32_t available_size = 0;
  uint32_t bytes_read = 0;
  uint32_t image_size = 0;        // during a download, once the server said
  std::string error;
  int64_t last_check_us = 0;      // 0 = never
  bool can_rollback = false;
  std::string rollback_version, rollback_partition;
};

bool start();
// Queues a release check; false when a check or install is running.
bool check_now();
// Queues an install: the available release (empty url), or any URL with an optional
// SHA256 (64 hex chars). False when busy or nothing is available.
bool install(const std::string &url = "", const std::string &sha256_hex = "");
// Makes the other slot bootable (its image must be valid); the caller reboots.
bool rollback(std::string &error);
Status status();
cJSON *status_json();
const char *state_name(State s);

}  // namespace p64::ota

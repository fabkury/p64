// p64 -- reliability bookkeeping (spec 15.3): why the device last rebooted, reboot
// counters by cause persisted in NVS (reset when an update proves good), the last crash's
// core dump summary, and the OTA image's validity (a new image is confirmed only once
// the firmware has run for a while, so a crashing update rolls back by itself).
#pragma once

#include <cstdint>
#include <string>

#include "cJSON.h"

namespace p64::system::reliability {

// At boot: classifies the reset, bumps its counter, reads the core dump summary.
void init();
// "power", "software", "panic", "watchdog", "brownout", "usb", "deep_sleep" or "other".
const char *reset_reason();
// True when a core dump from a previous crash is stored.
bool crash_present();
// {reset_reason, counters {power, software, ...}, crash {present, task, pc, cause,
// backtrace, ...}, image {partition, pending_verify, other_partition, other_version}}.
cJSON *json();
bool erase_coredump();
void reset_counters();
// The running image awaits confirmation (first boot after an update).
bool image_pending_verify();
// Confirms the running image (no rollback) and resets the reboot counters.
void mark_image_valid();
// Re-reads the other slot's description (after an install). Flash read: call it from a
// task with an internal-RAM stack only.
void refresh_image_info();

}  // namespace p64::system::reliability

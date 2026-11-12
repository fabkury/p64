// p64 -- the test build's fault-injection hooks (docs/testing-strategy/infrastructure.md).
// Everything here exists only with CONFIG_P64_TEST_HOOKS, which no release sets: the
// seams in the firmware are written `#ifdef CONFIG_P64_TEST_HOOKS` and compile to nothing
// otherwise, so the image that ships carries none of this.
//
// A hook changes what the code under test sees at a seam (the card did not mount, the
// clock has no answer, the worker is busy); it never replaces the logic being tested.
// The routes that set them are GET and POST /api/v1/test (p64_web/src/api.cpp).
#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_P64_TEST_HOOKS

#include <atomic>
#include <cstdint>

namespace p64::system::test {

// The time is withheld: net::clock answers "not synced" whatever NTP said. Persisted
// under kNoTimeKey, so a boot can run its whole length without a trusted time.
inline std::atomic<bool> time_withheld{false};
// The Makapix worker sleeps this long before its next job (one shot): what a long
// download or a held TLS slot does to whoever waits on the worker.
inline std::atomic<uint32_t> hold_worker_ms{0};
// The IMU does not answer its probe until the device has been up this long (seconds; set
// at boot from kNoImuKey): a sensor that misses its first probe and is found later.
inline std::atomic<uint32_t> imu_absent_s{0};

// Keys in the state store (NVS namespace p64state) that only a test build reads.
inline constexpr const char *kEjectKey = "t_eject";    // a reason: the next boots mount no card
inline constexpr const char *kNoTimeKey = "t_notime";  // "1": the time is withheld from boot
inline constexpr const char *kNoImuKey = "t_noimu";    // seconds: the IMU is not found before that uptime

}  // namespace p64::system::test

#endif  // CONFIG_P64_TEST_HOOKS

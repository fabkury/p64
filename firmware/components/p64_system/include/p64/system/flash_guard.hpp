// p64 -- flash operations from any task. Reading or writing the SPI flash (NVS, the
// partition table, OTA slots, the core dump) turns the instruction cache off for the
// duration, and a task whose stack lives in PSRAM cannot run then: the flash driver
// asserts (the WebSocket push task reading the reboot counters from NVS was a crash loop,
// 2026-09-19). on_internal_stack() runs `fn` directly when the caller's stack is
// internal, and otherwise on a short-lived helper task with a 4 KB internal stack,
// waiting for it. Every NVS access in the firmware goes through it, so PSRAM stacks are
// safe by construction and the scarce internal RAM is only borrowed for the operation.
#pragma once

#include <functional>

namespace p64::system {

bool on_internal_stack(const std::function<bool()> &fn);
// True when the calling task's stack is in external RAM.
bool stack_in_psram();

}  // namespace p64::system

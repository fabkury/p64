#include "p64/system/reliability.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "p64/system/state_store.hpp"

namespace p64::system::reliability {
namespace {

constexpr const char *TAG = "reliability";
const char *const kCauses[] = {"power", "software", "panic", "watchdog", "brownout", "usb", "deep_sleep", "other"};
constexpr int kCauseCount = sizeof(kCauses) / sizeof(kCauses[0]);

std::mutex g_mutex;
uint32_t g_counters[kCauseCount] = {};  // mirrored from NVS at init; NVS is written, never re-read
std::string g_other_partition, g_other_version, g_other_date;  // the other slot, read at init and after an install
const char *g_reason = "other";
bool g_crash = false;
std::string g_crash_task, g_crash_backtrace;
uint32_t g_crash_pc = 0, g_crash_cause = 0;
bool g_pending_verify = false;

const char *classify(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    default: return "other";
  }
}

std::string key_for(const char *cause) { return std::string("rb_") + cause; }

int cause_index(const char *cause) {
  for (int i = 0; i < kCauseCount; ++i) {
    if (std::strcmp(kCauses[i], cause) == 0) return i;
  }
  return kCauseCount - 1;
}

uint32_t counter_from_nvs(const char *cause) {
  std::string v;
  if (!state::get(key_for(cause).c_str(), v)) return 0;
  return static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
}

// Flash reads: only from a task whose stack lives in internal RAM (init, the OTA flash
// writer), never from json().
void read_other_partition() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *other = running ? esp_ota_get_next_update_partition(running) : nullptr;
  g_other_partition = other ? other->label : "";
  g_other_version.clear();
  g_other_date.clear();
  if (!other) return;
  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(other, &desc) == ESP_OK) {
    g_other_version = desc.version;
    g_other_date = desc.date;
  }
}

void read_crash() {
  if (esp_core_dump_image_check() != ESP_OK) return;
  auto *summary = static_cast<esp_core_dump_summary_t *>(std::calloc(1, sizeof(esp_core_dump_summary_t)));
  if (!summary) return;
  if (esp_core_dump_get_summary(summary) == ESP_OK) {
    g_crash = true;
    g_crash_task = summary->exc_task;
    g_crash_pc = summary->exc_pc;
    g_crash_cause = summary->ex_info.exc_cause;
    char buf[16];
    for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%08" PRIx32, i ? " " : "", summary->exc_bt_info.bt[i]);
      g_crash_backtrace += buf;
    }
    if (summary->exc_bt_info.corrupted) g_crash_backtrace += " (corrupted)";
    ESP_LOGW(TAG, "core dump present: task %s, pc 0x%08" PRIx32 ", cause %" PRIu32 ", backtrace %s", g_crash_task.c_str(),
             g_crash_pc, g_crash_cause, g_crash_backtrace.c_str());
  } else {
    g_crash = true;  // an image is there even if the summary could not be parsed
    ESP_LOGW(TAG, "core dump present, summary unreadable");
  }
  std::free(summary);
}

}  // namespace

void init() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_reason = classify(esp_reset_reason());
  for (int i = 0; i < kCauseCount; ++i) g_counters[i] = counter_from_nvs(kCauses[i]);
  const uint32_t n = ++g_counters[cause_index(g_reason)];
  state::set(key_for(g_reason).c_str(), std::to_string(n));
  read_other_partition();
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  g_pending_verify = running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  ESP_LOGI(TAG, "reset reason: %s (%" PRIu32 " so far)%s", g_reason, n, g_pending_verify ? "; image awaits confirmation" : "");
  read_crash();
}

const char *reset_reason() { return g_reason; }

bool crash_present() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_crash;
}

cJSON *json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "reset_reason", g_reason);
  cJSON *c = cJSON_AddObjectToObject(d, "counters");
  for (int i = 0; i < kCauseCount; ++i) cJSON_AddNumberToObject(c, kCauses[i], g_counters[i]);
  cJSON *crash = cJSON_AddObjectToObject(d, "crash");
  cJSON_AddBoolToObject(crash, "present", g_crash);
  if (g_crash) {
    cJSON_AddStringToObject(crash, "task", g_crash_task.c_str());
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08" PRIx32, g_crash_pc);
    cJSON_AddStringToObject(crash, "pc", buf);
    cJSON_AddNumberToObject(crash, "cause", g_crash_cause);
    cJSON_AddStringToObject(crash, "backtrace", g_crash_backtrace.c_str());
  }
  cJSON *image = cJSON_AddObjectToObject(d, "image");
  const esp_partition_t *running = esp_ota_get_running_partition();  // a table lookup in RAM
  cJSON_AddStringToObject(image, "partition", running ? running->label : "?");
  cJSON_AddBoolToObject(image, "pending_verify", g_pending_verify);
  if (!g_other_partition.empty()) {
    cJSON_AddStringToObject(image, "other_partition", g_other_partition.c_str());
    if (!g_other_version.empty()) {
      cJSON_AddStringToObject(image, "other_version", g_other_version.c_str());
      cJSON_AddStringToObject(image, "other_date", g_other_date.c_str());
    }
  }
  return d;
}

bool erase_coredump() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const esp_err_t err = esp_core_dump_image_erase();
  if (err == ESP_OK) {
    g_crash = false;
    g_crash_task.clear();
    g_crash_backtrace.clear();
    g_crash_pc = g_crash_cause = 0;
  }
  return err == ESP_OK;
}

void reset_counters() {
  for (int i = 0; i < kCauseCount; ++i) {
    g_counters[i] = 0;
    state::erase(key_for(kCauses[i]).c_str());
  }
}

void refresh_image_info() {
  std::lock_guard<std::mutex> lock(g_mutex);
  read_other_partition();
}

bool image_pending_verify() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_pending_verify;
}

void mark_image_valid() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_pending_verify) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    g_pending_verify = false;
    ESP_LOGI(TAG, "image confirmed; reboot counters reset");
    reset_counters();
  }
}

}  // namespace p64::system::reliability

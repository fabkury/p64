#include "p64/system/flash_guard.hpp"

#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace p64::system {
namespace {

constexpr const char *TAG = "flash_guard";

struct Job {
  const std::function<bool()> *fn;
  bool result;
  SemaphoreHandle_t done;
};

void helper(void *arg) {
  auto *job = static_cast<Job *>(arg);
  job->result = (*job->fn)();
  xSemaphoreGive(job->done);
  vTaskDelete(nullptr);
}

}  // namespace

bool stack_in_psram() {
  int marker = 0;
  return esp_ptr_external_ram(&marker);
}

bool on_internal_stack(const std::function<bool()> &fn) {
  if (!stack_in_psram()) return fn();
  Job job{&fn, false, xSemaphoreCreateBinary()};
  if (!job.done) return false;
  // Internal stack, the same core, one notch above the caller so it runs at once.
  const UBaseType_t prio = uxTaskPriorityGet(nullptr) + 1;
  if (xTaskCreatePinnedToCore(helper, "flash_op", 4096, &job, prio, nullptr, xPortGetCoreID()) != pdPASS) {
    ESP_LOGE(TAG, "no memory for the flash helper task");
    vSemaphoreDelete(job.done);
    return false;
  }
  xSemaphoreTake(job.done, portMAX_DELAY);
  vSemaphoreDelete(job.done);
  return job.result;
}

}  // namespace p64::system

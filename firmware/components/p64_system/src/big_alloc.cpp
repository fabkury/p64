// p64 -- the shell of big_alloc.hpp: the heap's figures, the lock, the allocation.
#include "p64/system/big_alloc.hpp"

#include <mutex>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace p64::system::big {

namespace {

const char *TAG = "big_alloc";
std::mutex g_mutex;
Stats g_stats;

}  // namespace

bool reserve(std::vector<uint8_t> &v, size_t capacity) {
  if (capacity <= v.capacity()) return true;
  if (capacity < kThreshold) {
    v.reserve(capacity);
    return true;
  }
  // Check and take under one lock: a download and an upload asking together see each
  // other. Whatever else allocates meanwhile is small, and the reserve is for it.
  std::lock_guard<std::mutex> lock(g_mutex);
  const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (!admits(capacity, free_bytes, largest)) {
    ++g_stats.refused;
    g_stats.last_refused_bytes = capacity;
    ESP_LOGW(TAG, "refused %u bytes: PSRAM has %u free, largest block %u, %u kept in reserve",
             static_cast<unsigned>(capacity), static_cast<unsigned>(free_bytes), static_cast<unsigned>(largest),
             static_cast<unsigned>(kReserve));
    return false;
  }
  v.reserve(capacity);
  ++g_stats.granted;
  return true;
}

bool resize(std::vector<uint8_t> &v, size_t size) {
  if (!reserve(v, size)) return false;
  v.resize(size);
  return true;
}

Stats stats() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_stats;
}

}  // namespace p64::system::big

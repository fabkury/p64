// p64 -- an allocator that puts containers in PSRAM on the device (plain malloc on the
// host). Channel indexes are large and long-lived; internal RAM is for Wi-Fi and TLS.
#pragma once

#include <cstddef>
#include <cstdlib>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace p64::content {

template <typename T>
struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template <typename U>
  PsramAllocator(const PsramAllocator<U> &) {}

  T *allocate(size_t n) {
    void *p = nullptr;
#if defined(ESP_PLATFORM)
    p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!p) p = std::malloc(n * sizeof(T));
    if (!p) std::abort();  // out of memory is fatal here (exceptions are off on the device)
    return static_cast<T *>(p);
  }
  void deallocate(T *p, size_t) { std::free(p); }
};

template <typename T, typename U>
bool operator==(const PsramAllocator<T> &, const PsramAllocator<U> &) {
  return true;
}
template <typename T, typename U>
bool operator!=(const PsramAllocator<T> &, const PsramAllocator<U> &) {
  return false;
}

}  // namespace p64::content

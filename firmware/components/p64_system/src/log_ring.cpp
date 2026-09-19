#include "p64/system/log_ring.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace p64::system::logring {
namespace {

char *g_ring = nullptr;
size_t g_size = 0;
size_t g_head = 0;  // next write position
size_t g_written = 0;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t g_previous = nullptr;

void ring_write(const char *data, size_t len) {
  if (!g_ring || len == 0) return;
  if (len > g_size) {
    data += len - g_size;
    len = g_size;
  }
  portENTER_CRITICAL(&g_lock);
  const size_t first = std::min(len, g_size - g_head);
  std::memcpy(g_ring + g_head, data, first);
  if (first < len) std::memcpy(g_ring, data + first, len - first);
  g_head = (g_head + len) % g_size;
  g_written += len;
  portEXIT_CRITICAL(&g_lock);
}

int hook(const char *fmt, va_list args) {
  char line[256];
  va_list copy;
  va_copy(copy, args);
  const int n = vsnprintf(line, sizeof(line), fmt, copy);
  va_end(copy);
  if (n > 0) ring_write(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
  return g_previous ? g_previous(fmt, args) : vprintf(fmt, args);
}

}  // namespace

void init(size_t bytes) {
  if (g_ring) return;
  g_ring = static_cast<char *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_ring) g_ring = static_cast<char *>(malloc(bytes));
  if (!g_ring) return;
  g_size = bytes;
  g_previous = esp_log_set_vprintf(hook);
}

std::string tail(size_t max_bytes) {
  std::string out;
  if (!g_ring) return out;
  portENTER_CRITICAL(&g_lock);
  const size_t have = g_written < g_size ? g_written : g_size;
  const size_t n = max_bytes < have ? max_bytes : have;
  out.resize(n);
  const size_t start = (g_head + g_size - n) % g_size;
  const size_t first = std::min(n, g_size - start);
  std::memcpy(out.data(), g_ring + start, first);
  if (first < n) std::memcpy(out.data() + first, g_ring, n - first);
  portEXIT_CRITICAL(&g_lock);
  // Start at a line boundary so the view never opens mid-line.
  const size_t nl = out.find('\n');
  if (nl != std::string::npos && nl + 1 < out.size() && n == max_bytes) out.erase(0, nl + 1);
  return out;
}

size_t total_written() { return g_written; }

}  // namespace p64::system::logring

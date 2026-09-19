#include "p64/system/event_bus.hpp"

#include <mutex>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace p64::system {
namespace {

constexpr const char *TAG = "events";

struct Subscription {
  Event type;
  Handler handler;
};

QueueHandle_t g_queue = nullptr;
TaskHandle_t g_task = nullptr;
std::mutex g_mutex;
std::vector<Subscription> g_subs;

void dispatcher(void *) {
  Message m;
  while (true) {
    if (xQueueReceive(g_queue, &m, portMAX_DELAY) != pdTRUE) continue;
    // Copy the matching handlers so a handler may subscribe others without deadlock.
    std::vector<Handler> handlers;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      for (const Subscription &s : g_subs) {
        if (s.type == m.type) handlers.push_back(s.handler);
      }
    }
    for (const Handler &h : handlers) h(m);
  }
}

}  // namespace

const char *event_name(Event type) {
  switch (type) {
    case Event::SettingsChanged:
      return "settings changed";
    case Event::WifiConnected:
      return "wifi connected";
    case Event::WifiDisconnected:
      return "wifi disconnected";
    case Event::SetupModeStarted:
      return "setup mode started";
    case Event::SetupModeStopped:
      return "setup mode stopped";
    case Event::TimeSynced:
      return "time synced";
    case Event::CardMounted:
      return "card mounted";
    case Event::CardFailed:
      return "card failed";
    case Event::PlaybackSwapped:
      return "playback swapped";
    case Event::MakapixStateChanged:
      return "makapix state changed";
  }
  return "?";
}

bool event_bus_init() {
  if (g_task) return true;
  g_queue = xQueueCreate(32, sizeof(Message));
  if (!g_queue) return false;
  const BaseType_t ok = xTaskCreatePinnedToCore(dispatcher, "events", 6144, nullptr, 5, &g_task, 0);
  return ok == pdPASS;
}

void subscribe(Event type, Handler handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_subs.push_back(Subscription{type, std::move(handler)});
}

bool publish(Event type, int32_t arg) {
  if (!g_queue) return false;
  const Message m{type, arg};
  if (xQueueSend(g_queue, &m, 0) != pdTRUE) {
    ESP_LOGW(TAG, "queue full; dropped %s", event_name(type));
    return false;
  }
  return true;
}

}  // namespace p64::system

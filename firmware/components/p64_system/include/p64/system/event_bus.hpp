// p64 -- EventBus: how components tell each other that something happened without
// knowing each other. publish() from any task or timer; handlers run one at a time on
// the bus task (core 0), so they may take mutexes but must not block for long.
#pragma once

#include <cstdint>
#include <functional>

namespace p64::system {

enum class Event : uint16_t {
  SettingsChanged = 1,  // the settings document was updated and persisted
  WifiConnected,        // arg: 0; status via net::wifi::status()
  WifiDisconnected,
  SetupModeStarted,     // the p64-setup access point is up
  SetupModeStopped,
  TimeSynced,           // NTP delivered the time (first time: arg 1)
  CardMounted,
  CardFailed,
  PlaybackSwapped,      // arg: history position
  MakapixStateChanged,
};

struct Message {
  Event type;
  int32_t arg;
};

using Handler = std::function<void(const Message &)>;

// Starts the dispatcher task. Safe to call more than once.
bool event_bus_init();
// Registers a handler for one event type; handlers are never unregistered (they belong
// to components that live for the whole run).
void subscribe(Event type, Handler handler);
// Queues an event; returns false when the queue is full (the event is dropped and logged).
bool publish(Event type, int32_t arg = 0);

const char *event_name(Event type);

}  // namespace p64::system

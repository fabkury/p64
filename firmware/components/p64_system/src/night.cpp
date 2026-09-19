#include "p64/system/night.hpp"

#include <algorithm>

namespace p64::system::night {

bool in_window(uint16_t start_minutes, uint16_t end_minutes, uint16_t now_minutes) {
  if (start_minutes == end_minutes) return false;  // an empty window
  if (start_minutes < end_minutes) return now_minutes >= start_minutes && now_minutes < end_minutes;
  return now_minutes >= start_minutes || now_minutes < end_minutes;  // crosses midnight
}

uint8_t effective_brightness(const Settings &s, int local_minutes, bool &night_active) {
  night_active = s.night.enabled && local_minutes >= 0 &&
                 in_window(s.night.start_minutes, s.night.end_minutes, static_cast<uint16_t>(local_minutes));
  const uint8_t wanted = night_active ? s.night.brightness : s.brightness;
  if (wanted == 0) return 0;  // panel off
  return std::min(wanted, s.brightness_ceiling);
}

}  // namespace p64::system::night

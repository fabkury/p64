// The weather icons (assets/weather, generated into weather_icons.cpp): one per WMO
// weather-code group, day and night, 24 and 12 pixels square, RGBA.
#pragma once

#include <cstddef>
#include <cstdint>

#include "p64/gfx/frame.hpp"

namespace p64::widgets::icons {

enum class Group : uint8_t { Clear, Partly, Overcast, Fog, Drizzle, Rain, Freezing, Snow, Showers, SnowShowers, Thunder, Hail };

struct Icon {
  Group group;
  bool night;
  uint8_t size;
  const uint8_t *rgba;  // size*size*4 bytes
};

extern const Icon kIcons[];
extern const size_t kIconCount;

// WMO weather interpretation codes (Open-Meteo) to a group.
Group group_for_code(int code);
const char *group_name(Group g);
const Icon *find(Group g, bool night, int size);
// Draws an icon with alpha over the frame at (x, y).
void draw(gfx::Frame &frame, const Icon &icon, int x, int y);

}  // namespace p64::widgets::icons

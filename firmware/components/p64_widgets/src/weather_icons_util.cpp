#include "weather_icons.hpp"

namespace p64::widgets::icons {

Group group_for_code(int code) {
  // WMO 4677 interpretation codes as Open-Meteo uses them.
  if (code == 0) return Group::Clear;
  if (code == 1 || code == 2) return Group::Partly;
  if (code == 3) return Group::Overcast;
  if (code == 45 || code == 48) return Group::Fog;
  if (code >= 51 && code <= 55) return Group::Drizzle;
  if (code == 56 || code == 57 || code == 66 || code == 67) return Group::Freezing;
  if (code >= 61 && code <= 65) return Group::Rain;
  if (code >= 71 && code <= 77) return Group::Snow;
  if (code >= 80 && code <= 82) return Group::Showers;
  if (code == 85 || code == 86) return Group::SnowShowers;
  if (code == 95) return Group::Thunder;
  if (code == 96 || code == 99) return Group::Hail;
  return Group::Overcast;
}

const char *group_name(Group g) {
  switch (g) {
    case Group::Clear: return "clear";
    case Group::Partly: return "partly cloudy";
    case Group::Overcast: return "overcast";
    case Group::Fog: return "fog";
    case Group::Drizzle: return "drizzle";
    case Group::Rain: return "rain";
    case Group::Freezing: return "freezing rain";
    case Group::Snow: return "snow";
    case Group::Showers: return "rain showers";
    case Group::SnowShowers: return "snow showers";
    case Group::Thunder: return "thunderstorm";
    case Group::Hail: return "thunderstorm with hail";
  }
  return "";
}

const Icon *find(Group g, bool night, int size) {
  const Icon *fallback = nullptr;
  for (size_t i = 0; i < kIconCount; ++i) {
    const Icon &ic = kIcons[i];
    if (ic.group != g || ic.size != size) continue;
    if (ic.night == night) return &ic;
    fallback = &ic;
  }
  return fallback;
}

void draw(gfx::Frame &frame, const Icon &icon, int x, int y) {
  const uint8_t *p = icon.rgba;
  for (int row = 0; row < icon.size; ++row) {
    for (int col = 0; col < icon.size; ++col, p += 4) {
      if (p[3] == 0) continue;
      const int px = x + col, py = y + row;
      if (px < 0 || py < 0 || px >= gfx::Frame::width() || py >= gfx::Frame::height()) continue;
      if (p[3] == 255) {
        frame.set(px, py, gfx::Rgb{p[0], p[1], p[2]});
      } else {
        const gfx::Rgb under = frame.get(px, py);
        const unsigned a = p[3], ia = 255 - a;
        frame.set(px, py, gfx::Rgb{static_cast<uint8_t>((p[0] * a + under.r * ia) / 255),
                                   static_cast<uint8_t>((p[1] * a + under.g * ia) / 255),
                                   static_cast<uint8_t>((p[2] * a + under.b * ia) / 255)});
      }
    }
  }
}

}  // namespace p64::widgets::icons

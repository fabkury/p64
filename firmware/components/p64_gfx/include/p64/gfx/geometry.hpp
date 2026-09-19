// p64 -- panel geometry and orientation. The panel's size is one property, read from
// the HUB75 configuration when building for the device and fixed at 64x64 on the host.
#pragma once

#include <cstdint>

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

namespace p64::gfx {

#if defined(CONFIG_HUB75_PANEL_WIDTH)
constexpr int kPanelWidth = CONFIG_HUB75_PANEL_WIDTH * CONFIG_HUB75_LAYOUT_COLS;
constexpr int kPanelHeight = CONFIG_HUB75_PANEL_HEIGHT * CONFIG_HUB75_LAYOUT_ROWS;
#else
constexpr int kPanelWidth = 64;
constexpr int kPanelHeight = 64;
#endif

// Logical orientation of the picture, clockwise. The panel is square, so the logical
// frame keeps the panel's size at every rotation.
enum class Rotation : uint16_t { R0 = 0, R90 = 90, R180 = 180, R270 = 270 };

constexpr bool valid_rotation(int degrees) {
  return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

}  // namespace p64::gfx

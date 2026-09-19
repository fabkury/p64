// p64 -- Settings: the one document of user settings (spec section 16), typed, kept in
// NVS as JSON, read as snapshots and written through update(). Every change is
// announced on the event bus (Event::SettingsChanged) after it is persisted.
//
// Wi-Fi credentials and Makapix credentials are not here: they live in their own NVS
// namespaces, owned by p64_net and p64_makapix.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "p64/gfx/frame.hpp"

namespace p64::system {

enum class PanelMode : uint8_t { Quality = 0, Photo = 1 };
enum class MainState : uint8_t { AnimationShow = 0, Widget = 1, Stream = 2 };
enum class PickMode : uint8_t { Random = 0, Recency = 1 };
enum class ChannelSelect : uint8_t { Stochastic = 0, Swrr = 1 };
enum class Corner : uint8_t { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 };
enum class WidgetKind : uint8_t { Clock = 0, Weather = 1, Temperature = 2 };

struct Settings {
  // Display
  uint8_t brightness = 255;          // 1..255
  uint8_t brightness_ceiling = 255;  // 1..255
  struct Night {
    bool enabled = false;
    uint16_t start_minutes = 22 * 60;  // minutes after midnight, local time
    uint16_t end_minutes = 7 * 60;
    uint8_t brightness = 0;  // 1..255, or 0 = panel off
  } night;
  PanelMode panel_mode = PanelMode::Quality;
  uint16_t rotation = 90;      // 0, 90, 180, 270
  bool rotation_auto = false;  // IMU decides; `rotation` is the fallback
  gfx::Rgb background{0, 0, 0};
  uint8_t gain_r = 100, gain_g = 100, gain_b = 100;  // 50..100 %
  uint16_t boot_animation_ms = 2000;                 // 0..5000

  // Show
  MainState main_state = MainState::AnimationShow;
  uint32_t auto_swap_seconds = 30;  // 0, or 5..86400
  PickMode pick_mode = PickMode::Random;
  ChannelSelect channel_select = ChannelSelect::Stochastic;
  struct ClockOverlay {
    bool enabled = true;
    std::string font = "capital-hill";
    Corner corner = Corner::TopLeft;
    bool h24 = true;
    gfx::Rgb colour{255, 255, 255};
  } clock_overlay;

  // Widgets
  WidgetKind widget = WidgetKind::Clock;
  uint8_t interlude_clock = 0, interlude_weather = 0, interlude_temperature = 0;  // 0..100 %
  struct Clock {
    bool analogue = false;  // face: digital (default) or analogue (drawn later)
    std::string font = "capital-hill";
    uint8_t scale = 2;      // 1..3
    bool seconds = false;
    bool blink_colon = false;
    bool h24 = true;
    bool month_first = false;  // date order: day-month (default) or month-day
    gfx::Rgb colour{255, 255, 255};
    gfx::Rgb background{0, 0, 0};
  } clock;
  struct Weather {
    bool location_set = false;
    float latitude = 0, longitude = 0;
    bool imperial = false;
    uint16_t refresh_minutes = 30;  // 10..180
  } weather;
  struct Temperature {
    int8_t offset_temperature = 0;  // -10..10 units
    int8_t offset_humidity = 0;
    bool trend = true;
  } temperature;

  // Stream
  bool stream_takeover = true;
  uint32_t stream_silence_ms = 5000;  // 500..60000
  bool ddp_enabled = true;
  uint16_t ddp_port = 4048;
  bool raw_udp_enabled = true;
  uint16_t raw_udp_port = 4064;

  // Inputs
  bool tap_enabled = true;
  uint8_t tap_sensitivity = 5;  // 1..10

  // Network
  std::string device_name;  // up to 16 chars [a-z0-9-]; "" = plain "p64"
  std::string timezone = "UTC";
  std::string ntp_server = "pool.ntp.org";

  // Storage
  std::string card_root = "/p64";
  uint16_t downloads_cap_mb = 64;  // 16..1024

  // Makapix
  uint32_t makapix_refresh_seconds = 14400;  // 60..86400
  uint16_t channel_cache_size = 2048;        // 32..4096

  // Updates
  bool auto_update_check = true;

  // Serialisation (the API's JSON shape). apply_json merges the keys present and
  // clamps every value into its range; unknown keys are ignored and reported.
  std::string to_json() const;
  bool apply_json(const char *json, std::string &error);
  void clamp();

  // "p64" or "p64-<device name>".
  std::string hostname() const;
};

// Loads the document from NVS (defaults for anything missing) at boot.
bool settings_init();
// A snapshot of the current settings.
Settings settings();
// Applies a change and persists it. The mutator sees the current document; the result
// is clamped, saved, and announced. Returns false when NVS refused the write.
bool settings_update(const std::function<void(Settings &)> &mutate);
// Restores the defaults (factory reset) and persists them.
bool settings_reset();

}  // namespace p64::system

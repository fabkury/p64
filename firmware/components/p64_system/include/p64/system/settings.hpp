// p64 -- Settings: the one document of user settings (spec section 16), typed, kept in
// NVS as JSON, read as snapshots and written through update(). Every change is
// announced on the event bus (Event::SettingsChanged) after it is persisted.
//
// Wi-Fi credentials and Makapix credentials are not here: they live in their own NVS
// namespaces, owned by p64_net and p64_makapix.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "p64/gfx/frame.hpp"

struct cJSON;  // apply_json's reader takes a parsed document

namespace p64::system {

// The values are display::Mode's (main.cpp and api.cpp cast one to the other).
enum class PanelMode : uint8_t { Quality = 0, Photo = 1 };
enum class MainState : uint8_t { AnimationShow = 0, Widget = 1, Stream = 2 };
enum class PickMode : uint8_t { Random = 0, Recency = 1 };
enum class ChannelSelect : uint8_t { Stochastic = 0, Swrr = 1 };
// The clock overlay's position: the four corners, or centred at the top or the bottom.
enum class Corner : uint8_t { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3, TopCenter = 4, BottomCenter = 5 };
enum class WidgetKind : uint8_t { Clock = 0, Weather = 1, Temperature = 2, Air = 3 };
constexpr int kWidgetKindCount = 4;
// The clock widget's face (spec 7.1): the digital and analogue ones honour the font and
// colour settings; the six themed ones (approved 2026-09-26) draw with their own assets.
enum class ClockFace : uint8_t { Digital = 0, Analogue = 1, Flip = 2, Nixie = 3, Horizon = 4, Words = 5, Hourglass = 6, Orrery = 7, Led = 8, HorizonRd = 9, Aquarium = 10, Bracket = 11, Station = 12 };
// The LED face's look (p053): four LED colours behind a filter, or a vacuum fluorescent display.
enum class LedStyle : uint8_t { Red = 0, Green = 1, Amber = 2, Blue = 3, Vfd = 4 };

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
  uint16_t boot_animation_ms = 3000;                 // 0 (off) or 1000..7000 (decided 2026-09-26)

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
    // The 1 px border around the text that makes it read over any artwork, blended over
    // the artwork at `border_opacity` (1..255; the text itself is always opaque).
    bool border = true;
    gfx::Rgb border_colour{0, 0, 0};
    uint8_t border_opacity = 255;
  } clock_overlay;

  // Widgets
  WidgetKind widget = WidgetKind::Clock;
  // Interludes (ADR 0014): the median gap in minutes between interludes of each kind,
  // 0 = never, else 5..1440; the per-swap probability follows from the auto-swap interval.
  uint16_t interlude_clock = 30, interlude_weather = 180, interlude_temperature = 0, interlude_air = 0;
  // A clock interlude draws a random face (p057): all nine, never the previous one again.
  bool interlude_random_clock_face = false;
  struct Clock {
    ClockFace face = ClockFace::Digital;
    LedStyle led_style = LedStyle::Red;  // the LED face only
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
  // The air widget (spec 7.4): the weather's location and refresh; the index it shows.
  struct Air {
    bool european = false;  // the European AQI instead of the US one
  } air;
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
  bool encoders_enabled = true;  // the p64b knobs act (they are always polled)
  bool encoders_swap = false;    // knob A takes knob B's role and vice versa
  bool encoders_invert = false;  // clockwise counts down

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
  uint16_t makapix_min_side = 16;            // 16, 32, 64 or 128: channels leave out narrower or shorter artworks
  uint16_t makapix_max_side = 128;           // 32, 64, 128 or 256: channels leave out wider or taller artworks
  uint16_t cache_retention_days = 30;        // 1..365: the nightly cache sweep deletes files not played for longer

  // Updates
  bool auto_update_check = true;

  // Serialisation (the API's JSON shape). apply_json merges the keys present and
  // clamps every value into its range; unknown keys are ignored and reported. It refuses
  // (false, nothing changed) malformed JSON and a minimum artwork size above the maximum;
  // `code` names which ("INVALID_JSON" or "INVALID_SETTINGS") for the API's reply.
  std::string to_json() const;
  bool apply_json(const char *json, std::string &error, const char **code = nullptr);
  void clamp();

  // The artwork size steps: any number snaps up to the next step (the maximum's 256 is
  // the canvas limit; the minimum's 128 the largest step below it).
  static uint16_t snap_min_side(uint16_t v) { return v <= 16 ? 16 : v <= 32 ? 32 : v <= 64 ? 64 : 128; }
  static uint16_t snap_max_side(uint16_t v) { return v <= 32 ? 32 : v <= 64 ? 64 : v <= 128 ? 128 : 256; }

  // apply_json's reader: merges a parsed document, false when the size steps conflict.
  bool read_json(const cJSON *root);

  // "p64" or "p64-<device name>".
  std::string hostname() const;
};

// The JSON names of the clock faces ("digital", "analogue", ...).
const char *clock_face_name(ClockFace face);
constexpr int kClockFaceCount = 13;

// Loads the document from NVS (defaults for anything missing) at boot.
bool settings_init();
// A snapshot of the current settings (a copy: six strings; fine off the hot paths).
Settings settings();
// The current settings without a copy: a shared, immutable document replaced on every
// update. For the per-frame paths (the overlay hook, the widget sources, the stream
// sink), which must not copy strings or wait on a lock a core-0 task holds for long.
std::shared_ptr<const Settings> settings_view();
// Applies a change and persists it. The mutator sees the current document; the result
// is clamped, saved, and announced. Returns false when NVS refused the write.
bool settings_update(const std::function<void(Settings &)> &mutate);
// Restores the defaults (factory reset) and persists them.
bool settings_reset();

}  // namespace p64::system

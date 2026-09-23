// Host unit tests: the night rule and the settings document.
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


// --- night schedule (spec 3.2) -----------------------------------------------------

TEST_CASE("night") {
  using namespace p64::system;
  CHECK(night::in_window(22 * 60, 7 * 60, 23 * 60));       // crosses midnight: 23:00 in
  CHECK(night::in_window(22 * 60, 7 * 60, 3 * 60));        // 03:00 in
  CHECK(!night::in_window(22 * 60, 7 * 60, 7 * 60));       // 07:00 out (end exclusive)
  CHECK(!night::in_window(22 * 60, 7 * 60, 12 * 60));
  CHECK(night::in_window(22 * 60, 7 * 60, 22 * 60));       // start inclusive
  CHECK(night::in_window(13 * 60, 14 * 60, 13 * 60 + 30)); // same day
  CHECK(!night::in_window(13 * 60, 14 * 60, 14 * 60));
  CHECK(!night::in_window(13 * 60, 13 * 60, 13 * 60));     // empty window
  Settings s;
  bool night = false;
  s.brightness = 200;
  s.brightness_ceiling = 150;
  CHECK_EQ(night::effective_brightness(s, 12 * 60, night), 150);  // ceiling caps
  CHECK(!night);
  s.night.enabled = true;
  s.night.start_minutes = 22 * 60;
  s.night.end_minutes = 7 * 60;
  s.night.brightness = 40;
  CHECK_EQ(night::effective_brightness(s, 23 * 60, night), 40);
  CHECK(night);
  CHECK_EQ(night::effective_brightness(s, 12 * 60, night), 150);
  CHECK(!night);
  CHECK_EQ(night::effective_brightness(s, -1, night), 150);        // time unknown: no schedule
  CHECK(!night);
  s.night.brightness = 0;                                            // panel off
  CHECK_EQ(night::effective_brightness(s, 2 * 60, night), 0);
  CHECK(night);
  s.night.brightness = 255;
  CHECK_EQ(night::effective_brightness(s, 2 * 60, night), 150);    // still capped
}


// --- the settings document (spec section 16; settings_model.cpp) ----------------------

using p64::system::Settings;

Settings applied(const char *json) {
  Settings s;
  std::string error;
  CHECK(s.apply_json(json, error));
  return s;
}

TEST_CASE("settings: defaults survive their own JSON") {
  const Settings a;
  Settings b;
  std::string error;
  CHECK(b.apply_json(a.to_json().c_str(), error));
  CHECK(b.to_json() == a.to_json());
  CHECK(a.hostname() == "p64");
}

TEST_CASE("settings: every field survives a round trip") {
  Settings a = applied(R"({"display":{"brightness":77,"brightness_ceiling":200,"night":{"enabled":true,
    "start_minutes":1320,"end_minutes":420,"brightness":0},"panel_mode":"photo","rotation":270,
    "rotation_auto":true,"background":{"r":1,"g":2,"b":3},"gains":{"r":60,"g":70,"b":80},"boot_animation_ms":0},
    "show":{"main_state":"widget","auto_swap_seconds":0,"pick_mode":"recency","channel_select":"swrr",
    "clock_overlay":{"enabled":false,"font":"everyday-typical","corner":"bottom_right","h24":false,
    "colour":{"r":9,"g":8,"b":7}}},"widgets":{"widget":"temperature","interlude_percent":{"clock":10,
    "weather":20,"temperature":30}},"stream":{"takeover":false,"silence_ms":900},
    "inputs":{"tap_enabled":false,"tap_sensitivity":9},"network":{"device_name":"desk-1","timezone":"America/Sao_Paulo"},
    "makapix":{"refresh_seconds":600,"channel_cache_size":512,"max_size":64,"cache_retention_days":7},
    "updates":{"auto_check":false}})");
  CHECK_EQ(a.brightness, 77);
  CHECK(a.night.enabled);
  CHECK_EQ(a.night.brightness, 0);  // 0 = panel off, allowed for the night only
  CHECK(a.panel_mode == p64::system::PanelMode::Photo);
  CHECK_EQ(a.rotation, 270);
  CHECK(a.main_state == p64::system::MainState::Widget);
  CHECK_EQ(a.auto_swap_seconds, 0u);  // 0 = no auto-swap
  CHECK(a.clock_overlay.corner == p64::system::Corner::BottomRight);
  CHECK(a.widget == p64::system::WidgetKind::Temperature);
  CHECK_EQ(a.makapix_max_side, 64);
  CHECK(a.hostname() == "p64-desk-1");
  CHECK(a.timezone == "America/Sao_Paulo");
  Settings b;
  std::string error;
  CHECK(b.apply_json(a.to_json().c_str(), error));
  CHECK(b.to_json() == a.to_json());
}

TEST_CASE("settings: out-of-range numbers clamp, never wrap (the M4 bug)") {
  const Settings a = applied(R"({"display":{"brightness":999,"brightness_ceiling":-5,"gains":{"r":10,"g":300,"b":75},
    "boot_animation_ms":70000,"night":{"start_minutes":5000}},"show":{"auto_swap_seconds":2},
    "clock":{"scale":9},"weather":{"refresh_minutes":1},"temperature":{"offset_temperature":-99},
    "stream":{"silence_ms":100},"inputs":{"tap_sensitivity":0},"storage":{"downloads_cap_mb":1},
    "makapix":{"refresh_seconds":1,"channel_cache_size":99999,"cache_retention_days":0}})");
  CHECK_EQ(a.brightness, 255);  // 999 would have wrapped to 231 as a uint8_t
  CHECK_EQ(a.brightness_ceiling, 1);
  CHECK_EQ(a.gain_r, 50);
  CHECK_EQ(a.gain_g, 100);
  CHECK_EQ(a.gain_b, 75);
  CHECK_EQ(a.boot_animation_ms, 5000);
  CHECK_EQ(a.night.start_minutes, 24 * 60 - 1);
  CHECK_EQ(a.auto_swap_seconds, 5u);
  CHECK_EQ(a.clock.scale, 3);
  CHECK_EQ(a.weather.refresh_minutes, 10);
  CHECK_EQ(a.temperature.offset_temperature, -10);
  CHECK_EQ(a.stream_silence_ms, 500u);
  CHECK_EQ(a.tap_sensitivity, 1);
  CHECK_EQ(a.downloads_cap_mb, 16);
  CHECK_EQ(a.makapix_refresh_seconds, 60u);
  CHECK_EQ(a.channel_cache_size, 4096);
  CHECK_EQ(a.cache_retention_days, 1);
}

TEST_CASE("settings: the maximum artwork size snaps up to 32, 64, 128 or 256") {
  CHECK_EQ(applied(R"({"makapix":{"max_size":1}})").makapix_max_side, 32);
  CHECK_EQ(applied(R"({"makapix":{"max_size":33}})").makapix_max_side, 64);
  CHECK_EQ(applied(R"({"makapix":{"max_size":100}})").makapix_max_side, 128);
  CHECK_EQ(applied(R"({"makapix":{"max_size":129}})").makapix_max_side, 256);
  CHECK_EQ(applied(R"({"makapix":{"max_size":5000}})").makapix_max_side, 256);
}

TEST_CASE("settings: invalid values fall back, unknown enum names leave the value alone") {
  const Settings a = applied(R"({"display":{"rotation":45,"panel_mode":"disco"},"show":{"main_state":"nap"},
    "weather":{"location_set":true,"latitude":123,"longitude":0},"network":{"device_name":"Bad Name!",
    "timezone":"","ntp_server":""},"storage":{"card_root":"relative"},"stream":{"ddp_port":0}})");
  CHECK_EQ(a.rotation, 90);
  CHECK(a.panel_mode == p64::system::PanelMode::Quality);
  CHECK(a.main_state == p64::system::MainState::AnimationShow);
  CHECK(!a.weather.location_set);
  CHECK(a.device_name.empty());
  CHECK(a.timezone == "UTC");
  CHECK(a.ntp_server == "pool.ntp.org");
  CHECK(a.card_root == "/p64");
  CHECK_EQ(a.ddp_port, 4048);
  CHECK(applied(R"({"network":{"device_name":"-edge"}})").device_name.empty());
  CHECK(applied(R"({"network":{"device_name":"seventeen-chars-x"}})").device_name.empty());
}

TEST_CASE("settings: a partial document merges, bad JSON is refused") {
  Settings s;
  s.brightness = 40;
  std::string error;
  CHECK(s.apply_json(R"({"inputs":{"tap_sensitivity":7},"nonsense":{"x":1}})", error));
  CHECK_EQ(s.brightness, 40);  // untouched keys keep their value
  CHECK_EQ(s.tap_sensitivity, 7);
  CHECK(!s.apply_json("{not json", error));
  CHECK(error == "invalid JSON");
  CHECK(!s.apply_json("[1,2]", error));
  CHECK(error == "expected an object");
}

}  // namespace

// The widgets' faces as pure drawing (see faces.cpp).
#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "p64/gfx/fonts.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/system/settings.hpp"
#include "p64/widgets/widgets.hpp"
#include "weather_model.hpp"

namespace p64::widgets::faces {

constexpr int64_t kSecond = 1000000;
constexpr int64_t kWeatherStaleUs = 6LL * 3600 * kSecond;  // spec 7.2: "no data" after 6 h without a refresh

// The font of that name, or the default one when unknown.
const gfx::fonts::Font &font_named(const std::string &name);
// The same for the clock overlay: a font not offered there (too tall for a corner) also
// falls back to the default.
const gfx::fonts::Font &overlay_font(const std::string &name);
std::string temperature_text(float value, bool decimals);

// The clock (digital or analogue) for `time`, or "--:--" / "NO TIME" when there is none.
// Returns how long the frame holds: to the next second or the next minute.
uint32_t draw_clock(gfx::Frame &out, const system::Settings &s, const tm *time);
// The clock overlay (spec 6.1) at `t`: a key that changes whenever the drawing would (the
// minute and every setting that shapes it: font, corner, 12/24 h, colour, outline), never
// 0; and the drawing, HH:MM in the chosen corner with an optional black outline.
uint32_t overlay_key(const system::Settings &s, const tm &t);
void draw_overlay(gfx::Frame &frame, const system::Settings &s, const tm &t);

// The overlay drawn once and stamped on every frame: the player draws it on each frame of
// an animation, and redrawing the glyphs (with the outline) and converting the time again
// each time cost about 380 us a frame on the device (2026-09-23). Built when the key
// changes; stamping gives the same pixels as draw_overlay(). About 4 KB: keep it in PSRAM.
struct OverlaySprite {
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // the mask's inked box, inclusive (empty when x1 < x0)
  gfx::Rgb text, outline;
  uint8_t mask[gfx::Frame::width() * gfx::Frame::height()] = {};  // gfx::fonts::kMaskText / kMaskOutline / 0
};
void build_overlay(OverlaySprite &out, const system::Settings &s, const tm &t);
void stamp_overlay(gfx::Frame &frame, const OverlaySprite &sprite);
// The weather: the forecast `f` as of `now` (monotonic, like f.fetched_us), or why not.
void draw_weather(gfx::Frame &out, const system::Settings &s, const weather_model::Forecast &f,
                  const std::string &error, int64_t now);
// The temperature and humidity reading, with the trend when the settings ask for it.
void draw_temperature(gfx::Frame &out, const system::Settings &s, const Reading &r);

}  // namespace p64::widgets::faces

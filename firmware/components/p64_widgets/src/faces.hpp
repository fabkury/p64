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

const gfx::fonts::Font &font_named(const std::string &name);
std::string temperature_text(float value, bool decimals);

// The clock (digital or analogue) for `time`, or "--:--" / "NO TIME" when there is none.
// Returns how long the frame holds: to the next second or the next minute.
uint32_t draw_clock(gfx::Frame &out, const system::Settings &s, const tm *time);
// The weather: the forecast `f` as of `now` (monotonic, like f.fetched_us), or why not.
void draw_weather(gfx::Frame &out, const system::Settings &s, const weather_model::Forecast &f,
                  const std::string &error, int64_t now);
// The temperature and humidity reading, with the trend when the settings ask for it.
void draw_temperature(gfx::Frame &out, const system::Settings &s, const Reading &r);

}  // namespace p64::widgets::faces

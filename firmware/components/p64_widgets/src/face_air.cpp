// The air widget's face (spec 7.4): the air quality index and the UV index in their band
// colours, the particulates, and the day's hourly index as bars under the UV curve. No
// ESP-IDF include; compared pixel for pixel with tools/mock_air_widget.py's references
// in tests/host/unit/air.cpp. Every font at its native size.
#include <algorithm>
#include <cstdio>
#include <string>

#include "faces.hpp"
#include "p64/gfx/fonts.hpp"
#include "sprite.hpp"

namespace p64::widgets::faces {
namespace {

using air_model::Scale;
using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kInk{255, 255, 255};
constexpr Rgb kDim{140, 140, 160};
constexpr Rgb kFaint{70, 70, 84};
constexpr int kGraphX = 8;  // 24 bars of 2 px
constexpr int kGraphTop = 43;
constexpr int kGraphH = 18;  // the bars end on row 60; the hour ticks sit on row 62
constexpr int kTickY = 62;
constexpr int kOtherHoursPct = 50;

const gfx::fonts::Font &font(const char *name) {
  const gfx::fonts::Font *f = gfx::fonts::by_name(name);
  return f ? *f : gfx::fonts::default_font();
}

Rgb dimmed(Rgb c, int pct) {
  const auto ch = [pct](uint8_t v) { return static_cast<uint8_t>((v * pct + 50) / 100); };
  return Rgb{ch(c.r), ch(c.g), ch(c.b)};
}

int round_half_up(double v) { return static_cast<int>(v + 0.5); }

// One decimal below 10, whole above; "--" for a value the reply did not have.
std::string number_text(float value) {
  if (value < 0) return "--";
  char buf[16];
  const double v = static_cast<double>(value);
  if (v < 9.95) {
    std::snprintf(buf, sizeof(buf), "%.1f", v);
  } else {
    std::snprintf(buf, sizeof(buf), "%d", round_half_up(v));
  }
  return buf;
}

void message(Frame &out, const char *second, const char *third, const std::string &fourth) {
  const gfx::fonts::Font &f = gfx::fonts::default_font();
  gfx::fonts::draw_centred(out, f, 20, "AIR", kInk, 1);
  gfx::fonts::draw_centred(out, f, 32, second, kDim, 1);
  if (third) gfx::fonts::draw_centred(out, f, 40, third, kDim, 1);
  if (!fourth.empty()) gfx::fonts::draw_centred(out, f, 44, fourth.substr(0, 10), kDim, 1);
}

}  // namespace

void draw_air(Frame &out, const system::Settings &s, const air_model::Air &a, const std::string &error, int64_t now,
              int64_t utc_now_s) {
  out.clear(gfx::kBlack);
  if (!s.weather.location_set) {
    message(out, "SET A", "LOCATION", "");
    return;
  }
  if (!a.valid || now - a.fetched_us > kWeatherStaleUs) {
    message(out, "NO DATA", nullptr, error);
    return;
  }
  const gfx::fonts::Font &small = font("everyday-slight");
  const gfx::fonts::Font &big = font("everyday-vast-black");
  const Scale scale = s.air.european ? Scale::EuropeanAqi : Scale::UsAqi;
  const int aqi = s.air.european ? a.eu_aqi : a.us_aqi;
  const int16_t *aqi_hours = s.air.european ? a.eu_hours : a.us_hours;
  const int w = Frame::width();

  // The two numbers: the air quality index on the left, the UV index on the right.
  const air_model::Band &aqi_band = air_model::band(scale, aqi < 0 ? 0 : aqi);
  const std::string aqi_text = aqi < 0 ? "--" : std::to_string(aqi);
  gfx::fonts::draw(out, big, 1, 1, aqi_text, aqi < 0 ? kDim : aqi_band.colour);
  const int uv = a.uv < 0 ? -1 : round_half_up(static_cast<double>(a.uv));
  const air_model::Band &uv_band = air_model::band(Scale::Uv, uv < 0 ? 0 : uv);
  const std::string uv_text = uv < 0 ? "--" : std::to_string(uv);
  gfx::fonts::draw(out, big, w - 1 - gfx::fonts::width(big, uv_text), 1, uv_text, uv < 0 ? kDim : uv_band.colour);
  // Their words, each after its label: the first from the left, the second to the right.
  int x = 1 + gfx::fonts::draw(out, small, 1, 15, "AQI", kDim) + 3;
  if (aqi >= 0) gfx::fonts::draw(out, small, x, 15, aqi_band.word, aqi_band.colour);
  const std::string uv_word = uv < 0 ? "" : uv_band.word;
  x = w - 1 - gfx::fonts::width(small, uv_word);
  gfx::fonts::draw(out, small, x, 22, uv_word, uv_band.colour);
  gfx::fonts::draw(out, small, x - 3 - gfx::fonts::width(small, "UV"), 22, "UV", kDim);
  // The particulates, and the data's age when it is old.
  gfx::fonts::draw(out, small, 1, 29, "PM2.5 " + number_text(a.pm2_5), kDim);
  gfx::fonts::draw(out, small, 1, 36, "PM10 " + number_text(a.pm10), kDim);
  const int64_t age_min = (now - a.fetched_us) / (60 * 1000000LL);
  if (age_min >= 90) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lldH", static_cast<long long>(age_min / 60));
    gfx::fonts::draw(out, small, w - 1 - gfx::fonts::width(small, buf), 36, buf, kDim);
  }

  // The day at the location: 24 bars of the hourly index in their band colours, the
  // hours that are not now dimmed; the UV index over them as a line.
  const int index = utc_now_s > 0 ? air_model::hour_index(a, utc_now_s) : -1;
  const int base = index < 0 ? 0 : index / 24 * 24;
  const int hour = index < 0 ? -1 : index % 24;
  if (base + 24 <= a.hours) {
    int peak = 0;
    double uv_peak = 0;
    for (int h = 0; h < 24; ++h) {
      peak = std::max<int>(peak, aqi_hours[base + h]);
      uv_peak = std::max(uv_peak, static_cast<double>(a.uv_hours[base + h]));
    }
    const int top = air_model::scale_top(scale, peak);
    for (int h = 0; h < 24; ++h) {
      const int v = aqi_hours[base + h];
      if (v < 0) continue;
      const int height = std::max(1, std::min(kGraphH, (v * kGraphH + top / 2) / top));
      Rgb colour = air_model::band(scale, v).colour;
      if (h != hour) colour = dimmed(colour, kOtherHoursPct);
      out.fill_rect(kGraphX + h * 2, kGraphTop + kGraphH - height, 2, height, colour);
    }
    const int uv_top = std::max(8, round_half_up(uv_peak));
    bool have_prev = false;
    int px = 0, py = 0;
    for (int h = 0; h < 24; ++h) {
      const double v = static_cast<double>(a.uv_hours[base + h]);
      if (v < 0.5) {
        have_prev = false;
        continue;
      }
      const int y = kGraphTop + kGraphH - 1 - std::min(kGraphH - 1, round_half_up(v * (kGraphH - 1) / uv_top));
      const int x0 = kGraphX + h * 2;
      if (have_prev) sprite::line(out, px, py, x0, y, kInk);
      out.set(x0, y, kInk);
      out.set(x0 + 1, y, kInk);
      px = x0 + 1;
      py = y;
      have_prev = true;
    }
  }
  // The hour ticks: midnight, 6, noon, 18 and midnight faint; now in white.
  for (int h = 0; h <= 24; h += 6) out.set(std::min(kGraphX + h * 2, kGraphX + 47), kTickY, kFaint);
  if (hour >= 0) out.fill_rect(kGraphX + hour * 2, kTickY, 2, 1, kInk);
}

}  // namespace p64::widgets::faces

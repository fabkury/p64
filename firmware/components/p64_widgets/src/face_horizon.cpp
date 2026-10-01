// The horizon face: a landscape computed from where the sun really is (solar.hpp): the
// sky's colours keyed by the sun's elevation, its twilight glow on the sun's side, the sun
// (a red disc near the horizon, gold with rays above), stars once the sun is far enough
// down, the moon with its phase, clouds by the weather's cover drifting with the minute,
// rain or snow, hills and a tree that darken at night (assets/clock/horizon), the time on
// top in outlined white (Everyday Vast Black) and the date on the ground. Floating point: the host test compares
// it with the mock within a tolerance rather than pixel for pixel.
#include <algorithm>
#include <cmath>

#include "clock_assets.hpp"
#include "p64/gfx/fonts.hpp"
#include "solar.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr int kHorizonY = 46;

struct Stop {
  float elevation;
  Rgb top, near, away;  // the top of the sky, the horizon near the sun, the horizon away from it
};
constexpr Stop kSky[] = {
    {-18, {2, 4, 18}, {8, 12, 38}, {8, 12, 38}},        {-12, {5, 6, 30}, {28, 18, 60}, {12, 14, 46}},
    {-6, {16, 16, 64}, {140, 60, 90}, {40, 30, 80}},    {-2, {30, 34, 100}, {245, 118, 60}, {90, 60, 110}},
    {2, {48, 74, 160}, {255, 168, 84}, {150, 120, 150}}, {8, {44, 100, 205}, {240, 205, 150}, {190, 190, 210}},
    {16, {38, 104, 226}, {165, 205, 245}, {165, 205, 245}}, {35, {30, 96, 224}, {150, 205, 250}, {150, 205, 250}},
    {90, {24, 86, 220}, {150, 205, 250}, {150, 205, 250}},
};
constexpr int kStars[][2] = {{5, 4},  {14, 9},  {22, 3},  {30, 12}, {41, 6},  {50, 2},  {57, 10},
                             {9, 16}, {36, 18}, {60, 20}, {26, 22}, {47, 15}, {3, 24},  {54, 27}};
constexpr int kCloudSpots[][2] = {{5, 20}, {40, 28}, {24, 14}, {52, 10}, {12, 32}, {34, 22}, {58, 30}};
constexpr int kCloudsByCover[4] = {1, 2, 4, 7};

Rgb lerp(Rgb a, Rgb b, float t) {
  const auto ch = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
  return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void sky_palette(float el, Rgb &top, Rgb &near, Rgb &away) {
  constexpr int n = sizeof(kSky) / sizeof(kSky[0]);
  if (el <= kSky[0].elevation) {
    top = kSky[0].top, near = kSky[0].near, away = kSky[0].away;
    return;
  }
  for (int i = 0; i + 1 < n; ++i) {
    const Stop &a = kSky[i], &b = kSky[i + 1];
    if (el <= b.elevation) {
      const float t = (el - a.elevation) / (b.elevation - a.elevation);
      top = lerp(a.top, b.top, t), near = lerp(a.near, b.near, t), away = lerp(a.away, b.away, t);
      return;
    }
  }
  top = kSky[n - 1].top, near = kSky[n - 1].near, away = kSky[n - 1].away;
}

// The viewer faces the equator: the sun rises on the left and sets on the right in the
// northern hemisphere (mirrored in the southern); the east-west component projected on the
// view plane, so a body near the zenith stays in the middle.
int sky_x(float az, float el, float lat) {
  float e = std::sin(az * 3.14159265f / 180) * std::cos(el * 3.14159265f / 180);
  if (lat < 0) e = -e;
  return static_cast<int>(std::lround(31.5f - 25.5f * e));
}

// The sun and the moon rise and set on the far hills' line, not on the horizon row. The
// day's arc is one curve of the elevation, the old one lifted onto a raised horizon (the
// higher of the hill line's two ends) and running to row 18 at the zenith, so it stays
// round and symmetric. Where the hills under a body are lower than that, the body sinks
// to their line instead, the difference fading out over the first 5 degrees (it only ever
// lowers a body near the line, so a body never dips while rising); where they are higher,
// they simply hide it. The setting: at +0.7 degrees the disc rests on the line, by -0.83
// (the almanac's sunset: the upper limb at the horizon, refraction included) it has slid
// behind it, under a pixel a minute for the sun in New York.
constexpr double kRests = 0.7, kSet = -0.83, kBlend = 5.0;
constexpr int kZenithY = 18;

// The far hills' top row in the frame per column, smoothed over five columns.
struct HillLine {
  double row[64];
  HillLine() {
    const sprite::View far = sprite::view(assets::kHorizonFarHills);
    int tops[64];
    for (int x = 0; x < 64; ++x) {
      int y = 0;
      while (y < far.h && !far.inked(x, y)) ++y;
      tops[x] = y + kHorizonY - 12;
    }
    for (int x = 0; x < 64; ++x) {
      int sum = 0;
      for (int d = -2; d <= 2; ++d) sum += tops[std::min(63, std::max(0, x + d))];
      row[x] = sum / 5.0;
    }
  }
};

bool body_y(const HillLine &line, double el, int x, int radius, int &y) {
  if (el <= kSet) return false;
  const double raised = std::min(line.row[0], line.row[63]);
  const double local = std::max(line.row[std::min(63, std::max(0, x))], raised);
  const double edge = raised + (local - raised) * std::max(0.0, 1 - (el - kRests) / kBlend);
  const double rests = edge - radius - 1, hidden = edge + radius;
  const double cy = el < kRests ? hidden + (rests - hidden) * (el - kSet) / (kRests - kSet)
                                : rests + (kZenithY - rests) * (std::min(el, 90.0) - kRests) / (90 - kRests);
  y = static_cast<int>(std::lround(cy));
  return true;
}

// A 9 px moon with its terminator; the lit side is the right one while waxing (mirrored in
// the southern hemisphere); the dark side shows faintly on a dark sky.
void draw_moon(Frame &frame, int x, int y, float phase, float lat, bool dark_sky) {
  const float c = std::cos(2 * 3.14159265f * phase);
  for (int dy = -4; dy <= 4; ++dy) {
    const float v = dy / 4.5f;
    const float s = std::sqrt(std::max(0.0f, 1 - v * v));
    for (int dx = -4; dx <= 4; ++dx) {
      const float u = dx / 4.5f;
      if (u * u + v * v > 1) continue;
      const float uu = lat >= 0 ? u : -u;
      const bool lit = phase < 0.5f ? uu > s * c : uu < -s * c;
      const int X = x + dx, Y = y + dy;
      if (X < 0 || X >= Frame::width() || Y < 0 || Y >= kHorizonY) continue;
      if (lit) {
        frame.set(X, Y, {236, 236, 248});
      } else if (dark_sky) {
        frame.set(X, Y, lerp(frame.get(X, Y), {60, 62, 90}, 0.6f));
      }
    }
  }
}

}  // namespace

int horizon_body_row(double elevation, int x, int radius) {
  int y = 0;
  return body_y(HillLine(), elevation, x, radius, y) ? y : -1;
}

void draw_horizon(Frame &frame, const Moment &m, const Options &o, const Sky &sky) {
  const float h = m.hour + m.minute / 60.0f;
  const solar::Position sun = solar::sun(sky.latitude, sky.longitude, sky.tz_hours, m.yday, h);
  const float el = sun.elevation;
  Rgb top, near, away;
  sky_palette(el, top, near, away);
  const float grey = 0.22f * sky.cover;
  top = lerp(top, {110, 116, 130}, grey);
  near = lerp(near, {110, 116, 130}, grey);
  away = lerp(away, {110, 116, 130}, grey);
  const int sx = sky_x(sun.azimuth, el, sky.latitude);
  const float glow_width = el < 12 ? 30.0f : 60.0f;
  for (int x = 0; x < Frame::width(); ++x) {
    const float d = (x - sx) / glow_width;
    const Rgb hz = lerp(away, near, std::exp(-d * d));
    for (int y = 0; y < kHorizonY; ++y) frame.set(x, y, lerp(top, hz, std::pow(y / (kHorizonY - 1.0f), 1.5f)));
  }
  frame.fill_rect(0, kHorizonY, Frame::width(), Frame::height() - kHorizonY, gfx::kBlack);
  const float light = clamp01((el + 6) / 14);
  // stars once the sun is 4 degrees down, brightest from 12 down, fewer under cloud
  const float star_k = clamp01((-el - 4) / 8) * (1 - 0.8f * std::min(1.0f, sky.cover / 2.0f));
  if (star_k > 0) {
    int i = 0;
    for (const auto &s : kStars) {
      const int x = s[0], y = s[1];
      frame.set(x, y, lerp(frame.get(x, y), {232, 232, 250}, star_k * (i % 3 ? 0.85f : 1.0f)));
      if (i % 3 == 0 && star_k > 0.6f) {
        constexpr int kCross[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &d : kCross) {
          const int X = x + d[0], Y = y + d[1];
          if (X >= 0 && X < Frame::width() && Y >= 0 && Y < kHorizonY)
            frame.set(X, Y, lerp(frame.get(X, Y), {150, 150, 190}, star_k));
        }
      }
      ++i;
    }
  }
  const HillLine line;
  const float phase = solar::moon_phase(m.year, m.yday, h, sky.tz_hours);
  const solar::Position moon = solar::moon(sky.latitude, sky.longitude, sky.tz_hours, m.year, m.yday, h);
  const int mx = sky_x(moon.azimuth, moon.elevation, sky.latitude);
  int my = 0;
  if (body_y(line, moon.elevation, mx, 4, my) && el < 25) draw_moon(frame, mx, my, phase, sky.latitude, el < -6);
  int sy = 0;
  if (body_y(line, el, sx, 3, sy)) {
    if (el < 6) {
      const float k = std::max(0.0f, el / 6);
      sprite::stamp(frame, sprite::view(assets::kHorizonSunLow), sx - 5, sy - 5, lerp({255, 110, 60}, {255, 222, 90}, k));
      frame.fill_rect(sx - 1, sy - 1, 3, 3, lerp({255, 150, 90}, {255, 240, 150}, k));
    } else {
      sprite::blit(frame, sprite::view(assets::kHorizonSun), sx - 5, sy - 5);
    }
  }
  Rgb tint = lerp({50, 52, 80}, lerp({255, 255, 255}, near, 0.35f), light);
  if (sky.cover >= 3) tint = lerp({40, 42, 60}, {168, 172, 186}, light);
  const int clouds = kCloudsByCover[sky.cover > 3 ? 3 : sky.cover];
  for (int i = 0; i < clouds; ++i) {
    const int x = (kCloudSpots[i][0] + m.minute * 64 / 60) % (Frame::width() + 13) - 13;
    sprite::stamp(frame, sprite::view(assets::kHorizonCloud), x, kCloudSpots[i][1], tint);
  }
  if (sky.precip != Sky::Precip::None) {
    uint32_t rng = static_cast<uint32_t>((m.minute * 7919 + m.hour * 104729) % 65536);
    for (int i = 0; i < 22; ++i) {
      rng = static_cast<uint32_t>((static_cast<uint64_t>(rng) * 1103515245u + 12345u) & 0x7fffffffu);
      const int x = (rng >> 8) % Frame::width(), y = (rng >> 4) % 44;
      if (sky.precip == Sky::Precip::Rain) {
        for (int k = 0; k < 3 && y + k < kHorizonY; ++k) frame.set(x, y + k, lerp(frame.get(x, y + k), {150, 190, 240}, 0.7f));
      } else {
        frame.set(x, y, {240, 244, 255});
      }
    }
  }
  sprite::stamp(frame, sprite::view(assets::kHorizonFarHills), 0, kHorizonY - 12,
                lerp(lerp({8, 14, 26}, {66, 128, 78}, light), away, 0.25f * light));
  sprite::stamp(frame, sprite::view(assets::kHorizonNearHills), 0, kHorizonY, lerp({4, 8, 14}, {32, 84, 46}, light));
  sprite::stamp(frame, sprite::view(assets::kHorizonTree), 49, 43, gfx::kBlack);
  if (sky.precip == Sky::Precip::Snow && light > 0)
    frame.fill_rect(0, kHorizonY, Frame::width(), 1, lerp({60, 64, 80}, {225, 230, 240}, light));
  const gfx::fonts::Font &font = gfx::fonts::default_font();
  const gfx::fonts::Font *big = gfx::fonts::by_name("everyday-vast-black");  // the time, at its native 11 px
  if (!big) big = &font;
  const Rgb white{255, 255, 255}, black{0, 0, 0};
  const std::string hh = o.h24 ? (m.hour < 10 ? "0" : "") + std::to_string(m.hour) : std::to_string(m.h12());
  const std::string mm = (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  const std::string time = hh + (colon_on(m, o) ? ":" : " ") + mm;
  // one extra pixel between the characters; the outlines first, then the ink, as one string draws
  constexpr int kGap = 1;
  const int spaced = gfx::fonts::width(*big, time) + kGap * (static_cast<int>(time.size()) - 1);
  const int x0 = (Frame::width() - spaced) / 2, x_end = x0 + spaced;
  for (const bool ink : {false, true}) {
    int x = x0;
    for (const char ch : time) {
      const std::string one(1, ch);
      if (ink) {
        gfx::fonts::draw(frame, *big, x, 4, one, white, 1);
      } else {
        gfx::fonts::draw(frame, *big, x, 4, one, black, 1, &black);
      }
      x += gfx::fonts::width(*big, one) + kGap;
    }
  }
  if (!o.h24) gfx::fonts::draw(frame, font, x_end - gfx::fonts::width(font, m.meridiem()), 18, m.meridiem(), white, 1, &black);
  gfx::fonts::draw_centred(frame, font, 55, date_text(m, o.month_first), lerp({150, 160, 190}, {230, 240, 230}, light), 1, &black);
}

}  // namespace p64::widgets::themed

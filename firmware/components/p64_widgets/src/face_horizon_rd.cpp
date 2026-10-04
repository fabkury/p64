// The Horizon-RD face (p075, p076): the horizon face's sky and almanac (its colours by the
// sun's elevation, the glow on the sun's side, the stars, the sun and the moon where they
// are, the moon's phase, the weather's clouds, rain and snow) over a landscape painted by
// Retro Diffusion: snow peaks, a lake, pines and a log cabin (assets/clock-png/horizon_rd,
// decoded when the face starts). The land keeps its own colours by day and sinks to a blue
// night by the daylight, warmed at the twilights; the lake mirrors the sky and glitters
// under the sun and the moon, the glints moving every kHorizonRdStepMs; the cabin's
// windows are lit while it is dark. The time on top, AM or PM beside it, the date small
// on the lake, clear of the cabin. Floating point: the host test compares it with the
// mock within a tolerance.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "horizon_sky.hpp"
#include "p64/gfx/fonts.hpp"
#include "solar.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;
using namespace horizon_sky;

constexpr int kLandY = 26;    // the land picture's first row in the frame
constexpr int kLakeTop = 51;  // the lake's first row
constexpr int kSkyRows = HorizonRdArt::kSkyRows;  // the gradient ends where the lake begins
constexpr int kZenithY = 18;
constexpr float kGlow = 9.5f;  // the radius of the sun's glow
constexpr float kNightLand[3] = {0.13f, 0.19f, 0.36f};  // what moonless night leaves of the land's colours
constexpr int kStars[][2] = {{5, 4},   {14, 9},  {22, 3},  {30, 12}, {41, 6},  {50, 2},  {57, 10}, {9, 16},  {36, 18}, {60, 20},
                             {26, 22}, {47, 15}, {3, 24},  {54, 27}, {18, 27}, {33, 6},  {44, 24}, {62, 4},  {1, 12},  {28, 30}};
constexpr int kCloudSpots[][2] = {{4, 19}, {38, 25}, {22, 13}, {50, 9}, {12, 29}};
constexpr int kCloudsByCover[4] = {1, 2, 3, 5};
constexpr int kCross[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// A colour scaled per channel.
Rgb mul(Rgb c, const float k[3]) {
  const auto ch = [](uint8_t v, float f) { return static_cast<uint8_t>(std::min(255L, std::lround(v * f))); };
  return {ch(c.r, k[0]), ch(c.g, k[1]), ch(c.b, k[2])};
}

// A small 32-bit hash of a pixel and a tick: the lake's glints.
uint32_t hash3(int x, int y, int t) {
  uint32_t v = (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u) ^ (static_cast<uint32_t>(t) * 83492791u);
  v = (v ^ (v >> 13)) * 1274126177u;
  return (v ^ (v >> 16)) & 0xFFFFu;
}

// face_horizon's body_y with this face's zenith row, the raised horizon being the
// skyline's median: the trees and the peaks above it simply hide a body.
bool body_y(const HorizonRdArt &art, double el, int x, int radius, int &y) {
  if (el <= kSet) return false;
  const double raised = art.raised;
  const double local = std::max(static_cast<double>(art.line[std::min(63, std::max(0, x))]), raised);
  const double edge = raised + (local - raised) * std::max(0.0, 1 - (el - kRests) / kBlend);
  const double rests = edge - radius - 1, hidden = edge + radius;
  const double cy = el < kRests ? hidden + (rests - hidden) * (el - kSet) / (kRests - kSet)
                                : rests + (kZenithY - rests) * (std::min(el, 90.0) - kRests) / (90 - kRests);
  y = static_cast<int>(std::lround(cy));
  return true;
}

// The painted moon with its terminator: the lit side keeps the picture's craters, the dark
// side shows faintly on a dark sky.
void draw_moon(Frame &frame, const sprite::View &moon, int x, int y, float phase, float lat, bool dark_sky) {
  constexpr float kLit[3] = {1.25f, 1.25f, 1.3f};
  const float r = moon.w / 2.0f;
  const float c = std::cos(2 * 3.14159265f * phase);
  for (int yy = 0; yy < moon.h; ++yy) {
    const float v = (yy + 0.5f - r) / r;
    const float s = std::sqrt(std::max(0.0f, 1 - v * v));
    for (int xx = 0; xx < moon.w; ++xx) {
      if (!moon.inked(xx, yy)) continue;
      const float u = (xx + 0.5f - r) / r;
      const float uu = lat >= 0 ? u : -u;
      const bool lit = phase < 0.5f ? uu > s * c : uu < -s * c;
      const int X = x - moon.w / 2 + xx, Y = y - moon.h / 2 + yy;
      if (lit) {
        frame.set(X, Y, mul(moon.colour(xx, yy), kLit));
      } else if (dark_sky) {
        frame.set(X, Y, lerp(frame.get(X, Y), {60, 62, 90}, 0.5f));
      }
    }
  }
}

// A sprite's inked pixels, each scaled by `k`.
void blit_scaled(Frame &frame, const sprite::View &v, int x, int y, const float k[3]) {
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx)
      if (v.inked(xx, yy)) frame.set(x + xx, y + yy, mul(v.colour(xx, yy), k));
}

}  // namespace

bool HorizonRdArt::load() {
  const bool ok = picture::decode(assets::kHorizonRdLandPng, land) && picture::decode(assets::kHorizonRdLakePng, lake) &&
                  picture::decode(assets::kHorizonRdLightsPng, lights) && picture::decode(assets::kHorizonRdSunPng, sun) &&
                  picture::decode(assets::kHorizonRdMoonPng, moon) && picture::decode(assets::kHorizonRdCloudAPng, cloud_a) &&
                  picture::decode(assets::kHorizonRdCloudBPng, cloud_b);
  if (!ok || land.w != Frame::width() || lake.w != land.w || lake.h != land.h || lights.w != land.w || lights.h != land.h ||
      kLandY + land.h > Frame::height())
    return false;
  // the skyline: the land's top row in the frame per column, smoothed over five columns
  const sprite::View v = land.view();
  int tops[64];
  for (int x = 0; x < 64; ++x) {
    int y = 0;
    while (y < v.h && !v.inked(x, y)) ++y;
    tops[x] = y + kLandY;
  }
  float sorted[64];
  for (int x = 0; x < 64; ++x) {
    int sum = 0;
    for (int d = -2; d <= 2; ++d) sum += tops[std::min(63, std::max(0, x + d))];
    line[x] = sorted[x] = sum / 5.0f;
  }
  std::sort(sorted, sorted + 64);
  raised = sorted[32];
  sky.assign(static_cast<size_t>(Frame::width()) * kSkyRows * 3, 0);
  mask.assign(static_cast<size_t>(Frame::width()) * Frame::height(), 0);
  return true;
}

void draw_horizon_rd(Frame &frame, const Moment &m, const Options &o, const Sky &place, HorizonRdArt &art, int millis) {
  const float h = m.hour + m.minute / 60.0f;
  const solar::Position sun = solar::sun(place.latitude, place.longitude, place.tz_hours, m.yday, h);
  const float el = sun.elevation;
  const int cover = place.cover > 3 ? 3 : place.cover;
  Rgb top, near, away;
  sky_palette(el, top, near, away);
  const float grey = 0.22f * cover;
  top = lerp(top, {110, 116, 130}, grey);
  near = lerp(near, {110, 116, 130}, grey);
  away = lerp(away, {110, 116, 130}, grey);
  const int sx = sky_x(sun.azimuth, el, place.latitude);
  const float glow_width = el < 12 ? 30.0f : 60.0f;
  float down[kSkyRows];  // how far down the gradient a row is
  for (int y = 0; y < kSkyRows; ++y) down[y] = std::pow(y / (kSkyRows - 1.0f), 1.5f);
  const auto sky_at = [&art](int x, int y) {
    const uint8_t *p = art.sky.data() + (static_cast<size_t>(x) * kSkyRows + y) * 3;
    return Rgb{p[0], p[1], p[2]};
  };
  frame.clear();
  for (int x = 0; x < Frame::width(); ++x) {
    const float d = (x - sx) / glow_width;
    const Rgb hz = lerp(away, near, std::exp(-d * d));
    for (int y = 0; y < kSkyRows; ++y) {
      const Rgb c = lerp(top, hz, down[y]);
      uint8_t *p = art.sky.data() + (static_cast<size_t>(x) * kSkyRows + y) * 3;
      p[0] = c.r, p[1] = c.g, p[2] = c.b;
      frame.set(x, y, c);
    }
  }
  const float light = daylight(el);
  const float star_k = clamp01((-el - 4) / 8) * (1 - 0.8f * std::min(1.0f, cover / 2.0f));
  if (star_k > 0) {
    int i = 0;
    for (const auto &s : kStars) {
      const int x = s[0], y = s[1];
      frame.set(x, y, lerp(frame.get(x, y), {232, 232, 250}, star_k * (i % 3 ? 0.85f : 1.0f)));
      if (i % 3 == 0 && star_k > 0.6f) {
        for (const auto &d : kCross) {
          const int X = x + d[0], Y = y + d[1];
          if (X >= 0 && X < Frame::width() && Y >= 0 && Y < kSkyRows) frame.set(X, Y, lerp(frame.get(X, Y), {150, 150, 190}, star_k));
        }
      }
      ++i;
    }
  }
  const sprite::View moon_v = art.moon.view(), sun_v = art.sun.view();
  const float phase = solar::moon_phase(m.year, m.yday, h, place.tz_hours);
  const solar::Position moon = solar::moon(place.latitude, place.longitude, place.tz_hours, m.year, m.yday, h);
  const int mx = sky_x(moon.azimuth, moon.elevation, place.latitude);
  int my = 0;
  const bool moon_up = body_y(art, moon.elevation, mx, moon_v.w / 2, my) && el < 25;
  if (moon_up) draw_moon(frame, moon_v, mx, my, phase, place.latitude, el < -6);
  int sy = 0;
  const bool sun_up = body_y(art, el, sx, sun_v.w / 2, sy);
  const Rgb sun_tint = lerp({255, 120, 90}, {255, 255, 255}, clamp01(el / 6));
  const float sun_k[3] = {sun_tint.r / 255.0f, sun_tint.g / 255.0f, sun_tint.b / 255.0f};
  if (sun_up) {
    // a soft glow around the disc, then the disc
    for (int dy = -9; dy <= 9; ++dy)
      for (int dx = -9; dx <= 9; ++dx) {
        const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        if (d < kGlow) frame.set(sx + dx, sy + dy, lerp(frame.get(sx + dx, sy + dy), sun_tint, 0.30f * (1 - d / kGlow)));
      }
    blit_scaled(frame, sun_v, sx - sun_v.w / 2, sy - sun_v.h / 2, sun_k);
  }
  Rgb tint = lerp({50, 52, 80}, lerp({255, 255, 255}, near, 0.35f), light);
  if (cover >= 3) tint = lerp({40, 42, 60}, {168, 172, 186}, light);
  const float tint_k[3] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f};
  for (int i = 0; i < kCloudsByCover[cover]; ++i) {
    const sprite::View cloud = (i % 2 ? art.cloud_b : art.cloud_a).view();
    const int x = (kCloudSpots[i][0] + m.minute * 64 / 60) % (Frame::width() + cloud.w) - cloud.w;
    blit_scaled(frame, cloud, x, kCloudSpots[i][1], tint_k);
  }
  if (place.precip != Sky::Precip::None) {
    uint32_t rng = static_cast<uint32_t>((m.minute * 7919 + m.hour * 104729) % 65536);
    for (int i = 0; i < 22; ++i) {
      rng = static_cast<uint32_t>((static_cast<uint64_t>(rng) * 1103515245u + 12345u) & 0x7fffffffu);
      const int x = (rng >> 8) % Frame::width(), y = (rng >> 4) % 50;
      if (place.precip == Sky::Precip::Rain) {
        for (int k = 0; k < 3; ++k) frame.set(x, y + k, lerp(frame.get(x, y + k), {150, 190, 240}, 0.7f));
      } else {
        frame.set(x, y, {240, 244, 255});
      }
    }
  }
  // the land: its own colours by day, a blue night, warmed by the twilight's glow
  const float warm[3] = {0.6f + 0.4f * near.r / 255, 0.6f + 0.4f * near.g / 255, 0.6f + 0.4f * near.b / 255};
  const float dusk = 0.5f * (1 - std::fabs(2 * light - 1));
  const int tick = (m.second * 1000 + millis) / static_cast<int>(kHorizonRdStepMs) + m.minute * 120;
  const sprite::View land = art.land.view(), lake = art.lake.view(), lights = art.lights.view();
  for (int yy = 0; yy < land.h; ++yy)
    for (int xx = 0; xx < land.w; ++xx) {
      if (!land.inked(xx, yy)) continue;
      Rgb c = land.colour(xx, yy);
      if (cover) {
        const uint8_t g = static_cast<uint8_t>((c.r * 3 + c.g * 6 + c.b) / 10);
        c = lerp(c, {g, g, g}, 0.15f * cover);
      }
      if (place.precip == Sky::Precip::Snow) c = lerp(c, {228, 232, 242}, 0.3f);
      Rgb lit = lerp(mul(c, kNightLand), c, light);
      lit = lerp(lit, mul(c, warm), dusk);
      const int y = yy + kLandY;
      if (lake.inked(xx, yy)) {
        // the lake mirrors the sky, and glitters under the sun and the moon
        lit = lerp(lit, sky_at(xx, std::max(0, kSkyRows - 1 - (y - kLakeTop) * 4)), 0.45f);
        const int spread = 1 + (y - kLakeTop) / 4;
        const uint32_t roll = hash3(xx, y, tick);
        if (sun_up && el < 30 && std::abs(xx - sx) <= spread && roll % 5 < 2) {
          lit = lerp(lit, mul({255, 236, 170}, sun_k), 0.5f);
        } else if (moon_up && el < -4 && std::abs(xx - mx) <= spread && roll % 5 < 2) {
          lit = lerp(lit, {214, 220, 240}, 0.4f);
        } else if (roll % 29 == 0) {
          lit = lerp(lit, {255, 255, 255}, 0.10f + 0.15f * light);
        }
      }
      frame.set(xx, y, lit);
    }
  // the cabin's windows, lit while it is dark, with a faint glow around them
  const float lamp = clamp01((0.35f - light) / 0.25f);
  if (lamp > 0) {
    for (int yy = 0; yy < lights.h; ++yy)
      for (int xx = 0; xx < lights.w; ++xx) {
        if (!lights.inked(xx, yy)) continue;
        for (const auto &d : kCross) {
          const int nx = xx + d[0], ny = yy + d[1];
          if (nx >= 0 && nx < lights.w && ny >= 0 && ny < lights.h && lights.inked(nx, ny)) continue;
          frame.set(nx, ny + kLandY, lerp(frame.get(nx, ny + kLandY), lights.colour(xx, yy), 0.22f * lamp));
        }
      }
    for (int yy = 0; yy < lights.h; ++yy)
      for (int xx = 0; xx < lights.w; ++xx)
        if (lights.inked(xx, yy)) frame.set(xx, yy + kLandY, lerp(frame.get(xx, yy + kLandY), lights.colour(xx, yy), lamp));
  }
  // The lettering: the horizon face's (white ink, one outline darkened once at 75 %),
  // laid out for a picture whose ground is worth seeing.
  const gfx::fonts::Font &font = gfx::fonts::default_font();
  const gfx::fonts::Font *big = gfx::fonts::by_name("everyday-vast-black");
  const gfx::fonts::Font *small = gfx::fonts::by_name("everyday-slight");
  if (!big) big = &font;
  if (!small) small = &font;
  const Rgb white{255, 255, 255};
  const std::string hh = o.h24 ? (m.hour < 10 ? "0" : "") + std::to_string(m.hour) : std::to_string(m.h12());
  const std::string mm = (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  const std::string time = hh + (colon_on(m, o) ? ":" : " ") + mm;
  constexpr int kGap = 1;
  constexpr uint8_t kOutlineAlpha = 191;  // 75 %
  const int spaced = gfx::fonts::width(*big, time) + kGap * (static_cast<int>(time.size()) - 1);
  const std::string mer = o.h24 ? "" : m.meridiem();
  const int mer_w = mer.empty() ? 0 : gfx::fonts::width(font, mer) + 2;
  const int x0 = (Frame::width() - spaced - mer_w) / 2;
  const std::string date = date_text(m, o.month_first);
  const int date_x = Frame::width() - 1 - gfx::fonts::width(*small, date);
  std::fill(art.mask.begin(), art.mask.end(), 0);
  for (const bool ink : {false, true}) {
    int x = x0;
    for (const char ch : time) {
      const std::string one(1, ch);
      if (ink) {
        gfx::fonts::draw(frame, *big, x, 3, one, white, 1);
      } else {
        gfx::fonts::draw_mask(art.mask.data(), *big, x, 3, one, 1, true);
      }
      x += gfx::fonts::width(*big, one) + kGap;
    }
    if (ink) {
      if (!mer.empty()) gfx::fonts::draw(frame, font, x0 + spaced + 2, 8, mer, white, 1);
      gfx::fonts::draw(frame, *small, date_x, 58, date, white, 1);
    } else {
      if (!mer.empty()) gfx::fonts::draw_mask(art.mask.data(), font, x0 + spaced + 2, 8, mer, 1, true);
      gfx::fonts::draw_mask(art.mask.data(), *small, date_x, 58, date, 1, true);
      for (int y = 0; y < Frame::height(); ++y)
        for (int xx = 0; xx < Frame::width(); ++xx)
          if (art.mask[static_cast<size_t>(y * Frame::width() + xx)] == gfx::fonts::kMaskOutline) frame.blend(xx, y, gfx::kBlack, kOutlineAlpha);
    }
  }
}

}  // namespace p64::widgets::themed

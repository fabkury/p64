// The beach face (p130, 2026-11-17): a seaside clock on one painting by Retro Diffusion
// (assets/clock-png/beach, decoded when the face starts): a palm's fronds hanging in from
// the top left, a lifeguard hut on stilts at the right, the sea across the middle, the
// sand in front; above it the sky faces' computed sky (horizon_sky.hpp, solar.cpp: the
// sun and the moon where they really are, the stars, the weather's clouds and rain).
// Nothing in the picture is ever regenerated, so nothing in it moves between the hours
// except what this file moves: the sea mirrors the sky and glitters under the sun and the
// moon (the sun sets into it); the tide follows the moon (the equilibrium tide's M2:
// cos 2H of the moon's hour angle; the waterline climbs the sand by up to two rows at the
// transits and leaves wet sand at the low; a foam line breaks along it every step); a
// sailing boat crosses the horizon and back every 23 minutes; a gull flies over every other minute by
// day; the umbrella and the towel are out from 09:00 to 18:00 in the sun; the crab comes
// out at the twilights; the campfire burns on the sand in the evening, its glow
// flickering; the hut's window is lit while it is dark; the land sinks to a blue night and
// is warmed at the twilights. A frame every kBeachStepMs. The time at the top right, the
// date at the bottom right, in the horizon faces' lettering. Floating point: the host test
// compares it with the mock (tools/mock_clock_faces.py, draw_beach) within a tolerance.
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

constexpr int kSkyRows = BeachArt::kSkyRows;  // the gradient ends at the sea's horizon
constexpr int kZenithY = 9;
constexpr int kSeaTop = 27;
constexpr float kGlow = 9.5f;
constexpr float kNightLand[3] = {0.13f, 0.19f, 0.36f};
constexpr float kWet[3] = {0.74f, 0.74f, 0.78f};  // what the wet sand keeps of the dry sand
constexpr int kStars[][2] = {{5, 4},   {14, 9},  {22, 3},  {30, 12}, {41, 6},  {50, 2},  {57, 10}, {9, 16},  {36, 18}, {60, 14},
                             {26, 22}, {47, 15}, {3, 24},  {33, 6},  {62, 4},  {1, 12},  {28, 25}, {54, 8},  {18, 20}, {44, 24}};
constexpr int kCloudSpots[][2] = {{30, 13}, {46, 5}, {10, 18}, {54, 16}, {36, 21}};
constexpr int kCloudsByCover[4] = {1, 2, 3, 5};
constexpr int kCross[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int kUmbrellaX = 12, kUmbrellaBottom = 59;
constexpr int kFireX = 14, kFireBottom = 56;
constexpr Rgb kFireGlow{255, 150, 50};
constexpr int kBoatBottom = 30;
constexpr int kBoatFrom = -5, kBoatTo = 41;  // it sails from off the left edge to the hut, and back
constexpr int kBoatSecondsPerPx = 30;         // a crossing in 23 minutes at the mean pace
constexpr float kBoatSway = 10, kBoatSwayPeriod = 3600;  // the pace breathes: +-10 px over an hour
constexpr int kBoatSail[][2] = {{2, 0}, {2, 1}, {3, 1}, {2, 2}, {3, 2}, {4, 2}, {1, 3}, {2, 3}, {3, 3}, {4, 3}};
constexpr Rgb kBoatHull{92, 62, 40};
constexpr int kCrabFrom = 20, kCrabTo = 38, kCrabBottom = 48;
constexpr Rgb kGull{40, 40, 52};
constexpr int kGullUp[][2] = {{0, 1}, {1, 0}, {2, 1}, {3, 0}, {4, 1}}, kGullDown[][2] = {{0, 0}, {1, 1}, {2, 0}, {3, 1}, {4, 0}};
constexpr int kRight = 62;  // the lettering's right edge
constexpr int kFireFrom = 19, kFireTo = 1;  // the campfire burns from 19:00 to 01:00, when it is dark
constexpr int kUmbrellaFrom = 9, kUmbrellaTo = 18;
constexpr int kCrabFrames = 4, kFireFrames = 7;

// A body's row at column x: the arc of the horizon faces over this face's skyline (the
// sea's horizon, the hut and the fronds above it hide a body).
bool body_y(const BeachArt &art, double el, int x, int radius, int &y) {
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

}  // namespace

bool BeachArt::load() {
  const bool ok = picture::decode(assets::kBeachLandPng, land) && picture::decode(assets::kBeachSeaPng, sea) &&
                  picture::decode(assets::kBeachLightsPng, lights) && picture::decode(assets::kBeachUmbrellaPng, umbrella) &&
                  picture::decode(assets::kBeachCrabPng, crab) && picture::decode(assets::kBeachFirePng, fire) &&
                  picture::decode(assets::kHorizonRdSunPng, sun) && picture::decode(assets::kHorizonRdMoonPng, moon) &&
                  picture::decode(assets::kHorizonRdCloudAPng, cloud_a) && picture::decode(assets::kHorizonRdCloudBPng, cloud_b);
  if (!ok || land.w != Frame::width() || land.h != Frame::height() || sea.w != land.w || sea.h != land.h || lights.w != land.w ||
      lights.h != land.h || crab.w % kCrabFrames || fire.w % kFireFrames)
    return false;
  const sprite::View v = land.view(), s = sea.view();
  int tops[64];
  for (int x = 0; x < 64; ++x) {
    int y = 0;
    while (y < v.h && !v.inked(x, y)) ++y;
    tops[x] = y;
    shore[x] = -1;
    for (int yy = s.h - 1; yy >= 0; --yy)
      if (s.inked(x, yy)) {
        shore[x] = static_cast<int8_t>(yy);
        break;
      }
  }
  for (int x = 0; x < 64; ++x) {
    int sum = 0;
    for (int d = -2; d <= 2; ++d) sum += tops[std::min(63, std::max(0, x + d))];
    line[x] = sum / 5.0f;
  }
  raised = kSeaTop;  // the sea's horizon: the hut and the fronds above it only hide a body
  sky.assign(static_cast<size_t>(Frame::width()) * kSkyRows * 3, 0);
  mask.assign(static_cast<size_t>(Frame::width()) * Frame::height(), 0);
  return true;
}

float beach_tide(const Sky &place, int year, int yday, float hour) {
  return std::cos(2 * solar::moon_hour_angle(place.latitude, place.longitude, place.tz_hours, year, yday, hour) * 3.14159265f / 180);
}

void draw_beach(Frame &frame, const Moment &m, const Options &o, const Sky &place, BeachArt &art, int millis) {
  const float h = m.hour + m.minute / 60.0f + m.second / 3600.0f;
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
  float down[kSkyRows];
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
  // the gull, every other minute by day, flapping every step
  const int tick = (m.second * 1000 + millis) / static_cast<int>(kBeachStepMs) + m.minute * (60000 / static_cast<int>(kBeachStepMs));
  if (el > 5) {
    const float t = (m.minute % 2) * 60 + m.second + millis / 1000.0f;
    if (t < 24) {
      const int gx = static_cast<int>(-6 + t * 3);
      const int gy = 9 + static_cast<int>(std::lround(2 * std::sin(t / 2)));
      const auto &wings = tick % 2 == 0 ? kGullUp : kGullDown;
      for (const auto &w : wings)
        if (gy + w[1] < kSkyRows) frame.set(gx + w[0], gy + w[1], kGull);
    }
  }
  // the land: its own colours by day, a blue night, warmed by the twilight's glow; the
  // sea mirrors the sky, glitters, and climbs the sand with the tide
  const float warm[3] = {0.6f + 0.4f * near.r / 255, 0.6f + 0.4f * near.g / 255, 0.6f + 0.4f * near.b / 255};
  const float dusk = 0.5f * (1 - std::fabs(2 * light - 1));
  const auto shade = [&](Rgb c) {
    if (cover) {
      const uint8_t g = static_cast<uint8_t>((c.r * 3 + c.g * 6 + c.b) / 10);
      c = lerp(c, {g, g, g}, 0.15f * cover);
    }
    if (place.precip == Sky::Precip::Snow) c = lerp(c, {228, 232, 242}, 0.3f);
    const Rgb lit = lerp(mul(c, kNightLand), c, light);
    return lerp(lit, mul(c, warm), dusk);
  };
  const int tide_rows = static_cast<int>(std::lround(2 * beach_tide(place, m.year, m.yday, h)));
  const sprite::View land = art.land.view(), sea = art.sea.view(), lights = art.lights.view();
  for (int yy = 0; yy < land.h; ++yy)
    for (int xx = 0; xx < land.w; ++xx) {
      if (!land.inked(xx, yy)) continue;
      Rgb c = land.colour(xx, yy);
      bool water = sea.inked(xx, yy);
      const int shore = art.shore[xx];
      if (shore >= 0) {
        if (tide_rows > 0 && shore < yy && yy <= shore + tide_rows) {
          water = true, c = land.colour(xx, shore);
        } else if (tide_rows < 0 && water && yy > shore + tide_rows) {
          water = false, c = mul(land.colour(xx, shore + 1), kWet);
        }
      }
      Rgb lit = shade(c);
      if (water) {
        lit = lerp(lit, sky_at(xx, std::max(0, kSkyRows - 1 - (yy - kSeaTop) * 2)), 0.45f);
        const int spread = 1 + (yy - kSeaTop) / 3;
        const uint32_t roll = hash3(xx, yy, tick);
        if (sun_up && el < 30 && std::abs(xx - sx) <= spread && roll % 5 < 2) {
          lit = lerp(lit, mul({255, 236, 170}, sun_k), 0.5f);
        } else if (moon_up && el < -4 && std::abs(xx - mx) <= spread && roll % 5 < 2) {
          lit = lerp(lit, {214, 220, 240}, 0.4f);
        } else if (roll % 29 == 0) {
          lit = lerp(lit, {255, 255, 255}, 0.10f + 0.15f * light);
        }
      }
      frame.set(xx, yy, lit);
    }
  // the foam along the waterline, breaking every step, on the water and the sand the
  // tide reaches and never on the hut (whose bottom edge sits a row above the shore)
  const auto beach_pixel = [&](int xx, int yy) {
    return yy >= 0 && yy < Frame::height() && (sea.inked(xx, yy) || (art.shore[xx] < yy && yy <= art.shore[xx] + tide_rows));
  };
  for (int xx = 0; xx < Frame::width(); ++xx) {
    if (art.shore[xx] < 0) continue;
    const int wl = art.shore[xx] + tide_rows;
    if (beach_pixel(xx, wl) && hash3(xx, wl, tick) % 4) frame.set(xx, wl, lerp(frame.get(xx, wl), {255, 255, 255}, 0.5f));
    if (beach_pixel(xx, wl - 1) && hash3(xx, wl - 1, tick) % 7 == 0) frame.set(xx, wl - 1, lerp(frame.get(xx, wl - 1), {255, 255, 255}, 0.3f));
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
  const Rgb white_shade = shade({255, 255, 255});
  const float shade_k[3] = {white_shade.r / 255.0f, white_shade.g / 255.0f, white_shade.b / 255.0f};
  // the boat on the horizon while the sun is up: it sails from the left edge to the hut
  // and back, a crossing every 23 minutes at the mean pace, which breathes over the hour;
  // counted from midnight; behind whatever the painting raises above the sea
  if (sun_up) {
    const int span = kBoatTo - kBoatFrom;
    const int t = (m.hour * 60 + m.minute) * 60 + m.second;
    const float u = t / static_cast<float>(kBoatSecondsPerPx) + kBoatSway * std::sin(2 * 3.14159265f * t / kBoatSwayPeriod);
    const int pos = ((static_cast<int>(std::floor(u)) % (2 * span)) + 2 * span) % (2 * span);
    const bool sailing_right = pos < span;
    const int bx = sailing_right ? kBoatFrom + pos : kBoatTo - (pos - span);
    const int by = kBoatBottom - 4;
    const auto boat_pixel = [&](int x, int y, Rgb c) {
      if (x >= 0 && x < Frame::width() && land.inked(x, y) && !sea.inked(x, y)) return;  // the hut or the fronds stand in front
      frame.set(x, y, c);
    };
    for (const auto &s : kBoatSail) boat_pixel(bx + (sailing_right ? s[0] : 4 - s[0]), by + s[1], mul({255, 255, 255}, shade_k));
    for (int dx = 0; dx < 5; ++dx) boat_pixel(bx + dx, kBoatBottom, mul(kBoatHull, shade_k));
  }
  // the umbrella and the towel, out in the day's sun
  if (m.hour >= kUmbrellaFrom && m.hour < kUmbrellaTo && el > 5) {
    const sprite::View u = art.umbrella.view();
    blit_scaled(frame, u, kUmbrellaX, kUmbrellaBottom - u.h + 1, shade_k);
  }
  // the crab, out at the twilights, walking the waterline forth and back
  if (light > 0.02f && light < 0.9f) {
    const int span = kCrabTo - kCrabFrom;
    const int pos = tick % (2 * span);
    const bool going_right = pos < span;
    const int cx = going_right ? kCrabFrom + pos : kCrabTo - (pos - span);
    const sprite::View f = art.crab.frame(kCrabFrames, tick % kCrabFrames);
    for (int yy = 0; yy < f.h; ++yy)
      for (int xx = 0; xx < f.w; ++xx) {
        const int src_x = going_right ? xx : f.w - 1 - xx;
        if (f.inked(src_x, yy)) frame.set(cx + xx, kCrabBottom - f.h + 1 + yy, mul(f.colour(src_x, yy), shade_k));
      }
  }
  // the campfire in the evening, its glow on the sand
  if (el < -4 && (m.hour >= kFireFrom || m.hour < kFireTo)) {
    const sprite::View f = art.fire.frame(kFireFrames, tick % kFireFrames);
    const int fy = kFireBottom - f.h + 1;
    const float cx = kFireX + f.w / 2.0f, cy = kFireBottom - 2;
    const float flicker = 0.75f + 0.25f * ((tick * 7) % 5) / 4;
    for (int y = static_cast<int>(cy) - 10; y < static_cast<int>(cy) + 6; ++y)
      for (int x = static_cast<int>(cx) - 11; x < static_cast<int>(cx) + 12; ++x) {
        const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * 1.6f * (y - cy) * 1.6f);
        if (d < 10 && x >= 0 && x < Frame::width() && y >= 0 && y < Frame::height())
          frame.set(x, y, lerp(frame.get(x, y), kFireGlow, 0.45f * flicker * (1 - d / 10)));
      }
    for (int yy = 0; yy < f.h; ++yy)
      for (int xx = 0; xx < f.w; ++xx)
        if (f.inked(xx, yy)) frame.set(kFireX + xx, fy + yy, f.colour(xx, yy));
  }
  // the hut's window, lit while it is dark, with a faint glow around it
  const float lamp = clamp01((0.35f - light) / 0.25f);
  if (lamp > 0) {
    for (int yy = 0; yy < lights.h; ++yy)
      for (int xx = 0; xx < lights.w; ++xx) {
        if (!lights.inked(xx, yy)) continue;
        for (const auto &d : kCross) {
          const int nx = xx + d[0], ny = yy + d[1];
          if (nx >= 0 && nx < lights.w && ny >= 0 && ny < lights.h && lights.inked(nx, ny)) continue;
          frame.set(nx, ny, lerp(frame.get(nx, ny), lights.colour(xx, yy), 0.22f * lamp));
        }
      }
    for (int yy = 0; yy < lights.h; ++yy)
      for (int xx = 0; xx < lights.w; ++xx)
        if (lights.inked(xx, yy)) frame.set(xx, yy, lerp(frame.get(xx, yy), lights.colour(xx, yy), lamp));
  }
  // The lettering: the horizon faces' (white ink, one outline darkened once at 75 %),
  // right-aligned at the top, clear of the palm; in 12 h the meridiem under the time.
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
  const int x0 = kRight - spaced;
  const std::string mer = o.h24 ? "" : m.meridiem();
  const int mer_x = kRight - gfx::fonts::width(font, mer);
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
      if (!mer.empty()) gfx::fonts::draw(frame, font, mer_x, 16, mer, white, 1);
      gfx::fonts::draw(frame, *small, date_x, 58, date, white, 1);
    } else {
      if (!mer.empty()) gfx::fonts::draw_mask(art.mask.data(), font, mer_x, 16, mer, 1, true);
      gfx::fonts::draw_mask(art.mask.data(), *small, date_x, 58, date, 1, true);
      for (int y = 0; y < Frame::height(); ++y)
        for (int xx = 0; xx < Frame::width(); ++xx)
          if (art.mask[static_cast<size_t>(y * Frame::width() + xx)] == gfx::fonts::kMaskOutline) frame.blend(xx, y, gfx::kBlack, kOutlineAlpha);
    }
  }
}

}  // namespace p64::widgets::themed

// The aquarium face (p075, p076): a goldfish tank painted by Retro Diffusion
// (assets/clock-png/aquarium, decoded when the face starts). A sixteen-frame loop of the
// tank (the plant sways, bubbles rise from the chest), three goldfish that swim from wall
// to wall and turn, each on its own lane and wagging its tail (eight frames played forth
// and back), and the time on the sunken sign (the time only: the board has no room for AM
// or PM). The far fish pass behind the castle, the plant, the chest and the sign; the near
// one in front of everything but the sign's board. The tank's lamp follows the sun: by
// night the water goes dark blue, the castle's window and door glow, the small fish hides
// and the others swim at half speed. Everything after the sun's elevation is integer
// arithmetic (the daylight in 256ths), the same as tools/mock_clock_faces.py: the host
// test compares pixel for pixel.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "horizon_sky.hpp"
#include "p64/gfx/fonts.hpp"
#include "solar.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr int kTankFrames = 16;
constexpr int kTankMs = static_cast<int>(kAquariumFrameMs);
constexpr int kBoardX = 27, kBoardY = 19;
constexpr Rgb kSignInk{255, 240, 200}, kSignShadow{58, 38, 30};
constexpr int kNightTank[3] = {77, 102, 179};  // in 256ths: what night leaves of red, green and blue
constexpr int kNightBelow = 77;                // the daylight (of 256) under which it is night for the fish
constexpr int kLampFrom = 90;                  // the castle's lights come on under this daylight, full 64 lower
constexpr int kFishFrames = 8;

struct Fish {
  int y0;            // the lane's row
  int x_min, x_max;  // the walls it turns at (the sprite's left edge)
  int speed;         // px per second
  int offset;        // the path's phase in px
  int bob, bob_ms;   // the rise and fall, px and its period
  bool near;         // in front of the decor
  bool shy;          // hidden at night
};
// In AquariumArt::fish's order: the small yellow one, the orange one, the large red and white one.
constexpr Fish kFishes[AquariumArt::kFishCount] = {
    {9, 6, 40, 7, 13, 3, 5200, false, true},
    {27, 6, 35, 4, 31, 2, 7900, false, false},
    {3, 5, 29, 3, 5, 1, 9700, true, false},
};

int sin_q(int tenths) { return assets::kSinQ14[tenths % 3600]; }
int q_to_px(int v) { return (v + (1 << (assets::kSinQ - 1))) >> assets::kSinQ; }

void draw_fish(Frame &frame, const picture::Picture &sheet, const Fish &f, int64_t t_ms) {
  const int span = f.x_max - f.x_min;
  const int p = static_cast<int>((t_ms * f.speed / 1000 + f.offset) % (2 * span));
  const bool right = p < span;
  const int x = right ? f.x_min + p : f.x_max - (p - span);
  const int y = f.y0 + q_to_px(f.bob * sin_q(static_cast<int>((t_ms % f.bob_ms) * 3600 / f.bob_ms)));
  int k = static_cast<int>((t_ms / kTankMs + f.offset) % (2 * kFishFrames - 2));
  if (k >= kFishFrames) k = 2 * kFishFrames - 2 - k;
  const sprite::View v = sheet.frame(kFishFrames, k);
  for (int yy = 0; yy < v.h; ++yy)
    for (int xx = 0; xx < v.w; ++xx) {
      const int sxx = right ? xx : v.w - 1 - xx;
      if (v.inked(sxx, yy)) frame.set(x + xx, y + yy, v.colour(sxx, yy));
    }
}

}  // namespace

bool AquariumArt::load() {
  picture::Picture front;
  const bool ok = picture::decode(assets::kAquariumTankPng, tank) && picture::decode(assets::kAquariumFrontPng, front) &&
                  picture::decode(assets::kAquariumSignPng, sign) && picture::decode(assets::kAquariumLightsPng, lights) &&
                  picture::decode(assets::kAquariumFishCPng, fish[0]) && picture::decode(assets::kAquariumFishAPng, fish[1]) &&
                  picture::decode(assets::kAquariumFishBPng, fish[2]);
  if (!ok || tank.w != kTankFrames * Frame::width() || tank.h != Frame::height() || front.w != tank.w || front.h != tank.h ||
      lights.w != Frame::width() || lights.h != Frame::height())
    return false;
  // One picture instead of two: the tank's alpha now says what stands in front of the far
  // fish (front.png's pixels are the tank's own), and the second 262 KB go back.
  for (size_t i = 3; i < tank.rgba.size(); i += 4) tank.rgba[i] = front.rgba[i];
  return true;
}

void draw_aquarium(Frame &frame, const Moment &m, const Options &o, const Sky &place, const AquariumArt &art, int millis) {
  const solar::Position sun = solar::sun(place.latitude, place.longitude, place.tz_hours, m.yday, m.hour + m.minute / 60.0f);
  const int light = static_cast<int>(std::lround(horizon_sky::daylight(sun.elevation) * 256));
  const bool night = light < kNightBelow;
  const int64_t t_ms = ((m.hour * 60 + m.minute) * 60 + m.second) * 1000LL + millis;
  const int64_t swim_ms = night ? t_ms / 2 : t_ms;
  const sprite::View tank = art.tank.frame(kTankFrames, static_cast<int>((t_ms / kTankMs) % kTankFrames));
  for (int y = 0; y < tank.h; ++y)
    for (int x = 0; x < tank.w; ++x) frame.set(x, y, tank.colour(x, y));
  for (int i = 0; i < AquariumArt::kFishCount; ++i)
    if (!kFishes[i].near && !(kFishes[i].shy && night)) draw_fish(frame, art.fish[i], kFishes[i], swim_ms);
  sprite::blit(frame, tank, 0, 0);  // the decor in front of the far fish
  for (int i = 0; i < AquariumArt::kFishCount; ++i)
    if (kFishes[i].near) draw_fish(frame, art.fish[i], kFishes[i], swim_ms);
  sprite::blit(frame, art.sign.view(), kBoardX, kBoardY);
  // the lamp: everything dims to a blue night, then the castle's lights and the time glow
  if (light < 256) {
    int dim[3];
    for (int i = 0; i < 3; ++i) dim[i] = kNightTank[i] + (((256 - kNightTank[i]) * light) >> 8);
    uint8_t *p = frame.pixels();
    for (size_t i = 0; i < Frame::bytes(); i += 3)
      for (int k = 0; k < 3; ++k) p[i + k] = static_cast<uint8_t>((p[i + k] * dim[k]) >> 8);
  }
  const int lamp = std::max(0, std::min(256, (kLampFrom - light) * 4));
  if (lamp > 0) {
    const uint8_t alpha = static_cast<uint8_t>(std::min(255, (lamp * 217) >> 8));  // 85 % at full
    const sprite::View lights = art.lights.view();
    for (int y = 0; y < lights.h; ++y)
      for (int x = 0; x < lights.w; ++x)
        if (lights.inked(x, y)) frame.blend(x, y, lights.colour(x, y), alpha);
  }
  const gfx::fonts::Font *font = gfx::fonts::by_name("everyday-standard");
  if (!font) font = &gfx::fonts::default_font();
  const std::string hh = o.h24 ? (m.hour < 10 ? "0" : "") + std::to_string(m.hour) : std::to_string(m.h12());
  const std::string text = hh + (colon_on(m, o) ? ":" : " ") + (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  const int x = kBoardX + (art.sign.w - gfx::fonts::width(*font, text) + 1) / 2;
  const int shade = 128 + light / 2;  // the carved shadow, darker at night
  const Rgb shadow{static_cast<uint8_t>((kSignShadow.r * shade) >> 8), static_cast<uint8_t>((kSignShadow.g * shade) >> 8),
                   static_cast<uint8_t>((kSignShadow.b * shade) >> 8)};
  gfx::fonts::draw(frame, *font, x + 1, kBoardY + 3, text, shadow, 1);
  gfx::fonts::draw(frame, *font, x, kBoardY + 2, text, kSignInk, 1);
}

}  // namespace p64::widgets::themed

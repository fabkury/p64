// The orrery face: on a star chart with brass rings (assets/clock/orrery: the plate, the
// Sun, the Earth, the Moon, Mercury) the Earth goes round the Sun once in twelve hours,
// the Moon round the Earth once an hour, Mercury round the Sun once a minute (with the
// seconds setting), each on a brass arm; the time in figures on the plaque. Positions
// come from the Q14 sine table in tenths of a degree, so the arithmetic is the mock's.
#include "clock_assets.hpp"
#include "p64/gfx/fonts.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kArm{150, 112, 44}, kArmDim{110, 82, 34}, kPlaqueInk{240, 208, 130};
constexpr int kQ = assets::kSinQ;
constexpr int kCxQ = 63 << (kQ - 1);  // 31.5 in Q14
constexpr int kCyQ = 55 << (kQ - 1);  // 27.5
constexpr int kEarthR = 20, kMercuryR = 10, kMoonR = 4;
constexpr int kHubX = 31, kHubY = 27;

int sin_q(int tenths) { return assets::kSinQ14[((tenths % 3600) + 3600) % 3600]; }
int cos_q(int tenths) { return sin_q(tenths + 900); }
int to_px(int q) { return (q + (1 << (kQ - 1))) >> kQ; }

}  // namespace

void orrery_positions(const Moment &m, int &ex, int &ey, int &mx, int &my, int &qx, int &qy) {
  const int ih = (m.hour % 12) * 300 + m.minute * 5;  // 30 degrees an hour
  const int im = m.minute * 60 + m.second;            // 6 degrees a minute
  const int is = m.second * 60;                       // 6 degrees a second
  const int ex_q = kCxQ + kEarthR * sin_q(ih), ey_q = kCyQ - kEarthR * cos_q(ih);
  ex = to_px(ex_q);
  ey = to_px(ey_q);
  mx = to_px(ex_q + kMoonR * sin_q(im));
  my = to_px(ey_q - kMoonR * cos_q(im));
  qx = to_px(kCxQ + kMercuryR * sin_q(is));
  qy = to_px(kCyQ - kMercuryR * cos_q(is));
}

void draw_orrery(Frame &frame, const Moment &m, const Options &o) {
  sprite::blit(frame, sprite::view(assets::kOrreryPlate), 0, 0);
  int ex, ey, mx, my, qx, qy;
  orrery_positions(m, ex, ey, mx, my, qx, qy);
  sprite::line(frame, kHubX, kHubY, ex, ey, kArm);
  if (o.seconds) sprite::line(frame, kHubX, kHubY, qx, qy, kArmDim);
  sprite::line(frame, ex, ey, mx, my, kArm);
  sprite::blit(frame, sprite::view(assets::kOrrerySun), 27, 23);
  sprite::blit(frame, sprite::view(assets::kOrreryEarth), ex - 2, ey - 2);
  sprite::blit(frame, sprite::view(assets::kOrreryMoon), mx - 1, my - 1);
  if (o.seconds) sprite::blit(frame, sprite::view(assets::kOrreryMercury), qx - 1, qy - 1);
  const gfx::fonts::Font &font = gfx::fonts::default_font();
  const std::string hh = o.h24 ? (m.hour < 10 ? "0" : "") + std::to_string(m.hour) : std::to_string(m.h12());
  const std::string mm = (m.minute < 10 ? "0" : "") + std::to_string(m.minute);
  gfx::fonts::draw_centred(frame, font, 57, hh + (colon_on(m, o) ? ":" : " ") + mm, kPlaqueInk);
  if (!o.h24) gfx::fonts::draw(frame, font, 63 - gfx::fonts::width(font, m.meridiem()), 57, m.meridiem(), kArm);
}

}  // namespace p64::widgets::themed

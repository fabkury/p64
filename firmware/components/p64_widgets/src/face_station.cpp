// The station clock face (p077, p078): an analogue face on a dial painted by Retro
// Diffusion, a railway station clock (a white dial in a steel rim;
// assets/clock/station/dial.png). Drawn on it: a bar at every hour (heavier at the
// quarters), a tick a minute, black bar hands with a lighter edge and a shadow, a hub,
// the date in grey, and a glint that crosses the glass every twelve seconds. With the
// seconds setting the red second hand runs the way station clocks do: once round in 58.5
// seconds, then it waits at the top for the minute hand to jump. Integer arithmetic
// (dial.hpp): pixel for pixel the mock's.
#include <algorithm>
#include <cstdlib>

#include "clock_assets.hpp"
#include "dial.hpp"
#include "p64/gfx/fonts.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr int kCx2 = 64, kCy2 = 63;  // the dial's centre, half pixels
constexpr int kDial2Sq = 57 * 57;    // the white dial, squared half pixels
constexpr Rgb kBlack{14, 14, 18}, kEdge{58, 58, 66}, kTick{96, 96, 104}, kDate{118, 118, 128}, kRed{222, 30, 34};
constexpr dial::Point kMinuteTick[] = {{1683, 32}, {1798, 32}};
constexpr dial::Point kHourBar[] = {{1408, 64}, {1798, 64}};
constexpr dial::Point kQuarterBar[] = {{1344, 96}, {1798, 96}};
constexpr dial::Point kHour[] = {{-256, 128}, {960, 128}};
constexpr dial::Point kMinute[] = {{-320, 96}, {1536, 96}};
constexpr dial::Shade kSteel{kBlack, 45, kEdge};
constexpr int kSecondLength2 = 38, kSecondTail2 = 14;  // to the disc's centre; half pixels
constexpr int kSweepMs = 58500;
constexpr int kGlintHalf = 11;  // the band's half width along the diagonal, half pixels
constexpr int kGlintAlpha = 70;
constexpr uint8_t kShadow = 46;

}  // namespace

int station_second_angle10(const Moment &m, int millis) {
  return std::min(3600, (m.second * 1000 + millis) * 3600 / kSweepMs) % 3600;
}

int station_glint_ms(const Moment &m, int millis) {
  return ((m.minute * 60 + m.second) * 1000 + millis) % static_cast<int>(kStationGlintEveryMs);
}

void draw_station(Frame &frame, const Moment &m, const Options &o, int millis) {
  sprite::blit(frame, sprite::view(assets::kStationDial), 0, 0);
  for (int i = 0; i < 60; ++i) {
    if (i % 5) {
      dial::draw_shape(frame, kCx2, kCy2, i * 60, dial::profile(kMinuteTick), dial::Shade{kTick});
    } else {
      dial::draw_shape(frame, kCx2, kCy2, i * 60, i % 15 ? dial::profile(kHourBar) : dial::profile(kQuarterBar), dial::Shade{kBlack});
    }
  }
  const gfx::fonts::Font *small = gfx::fonts::by_name("everyday-slight");
  if (!small) small = &gfx::fonts::default_font();
  const std::string date = date_text(m, o.month_first);
  gfx::fonts::draw(frame, *small, (Frame::width() - gfx::fonts::width(*small, date)) / 2, 37, date, kDate, 1);
  dial::draw_shape(frame, kCx2, kCy2, (m.hour % 12) * 300 + m.minute * 5, dial::profile(kHour), kSteel, kShadow);
  dial::draw_shape(frame, kCx2, kCy2, m.minute * 60, dial::profile(kMinute), kSteel, kShadow);
  dial::disc(frame, kCx2, kCy2, 27, [](int, int) { return kBlack; });
  if (o.seconds) {
    const int a = station_second_angle10(m, millis);
    dial::thin_hand(frame, kCx2, kCy2, a, kSecondLength2, kSecondTail2, kRed);
    int tx2, ty2;
    dial::polar_half(kCx2, kCy2, a, kSecondLength2, tx2, ty2);
    dial::disc(frame, tx2, ty2, 27, [](int, int) { return kRed; });
    dial::disc(frame, kCx2, kCy2, 7, [](int, int) { return kRed; });
  }
  // the glint: a band of light crossing the glass from the upper left
  const int t = station_glint_ms(m, millis);
  if (t < static_cast<int>(kStationGlintMs)) {
    const int pos = -124 + 248 * t / static_cast<int>(kStationGlintMs);
    for (int y = 0; y < Frame::height(); ++y)
      for (int x = 0; x < Frame::width(); ++x) {
        const int dx = 2 * x - kCx2, dy = 2 * y - kCy2;
        const int d = std::abs(dx + dy - pos);
        if (dx * dx + dy * dy <= kDial2Sq && d < kGlintHalf)
          frame.blend(x, y, {255, 255, 255}, static_cast<uint8_t>(kGlintAlpha * (kGlintHalf - d) / kGlintHalf));
      }
  }
}

}  // namespace p64::widgets::themed

// The bracket clock face (p077, p078): an analogue face on a dial painted by Retro
// Diffusion, the face of an antique bracket clock (an engraved brass plate whose corners
// show around a cream enamel dial; assets/clock/bracket/dial.png). Drawn on it: Roman
// cardinals (Capital Hill, whose I has serifs) and a diamond at the other hours, a dot a
// minute, blued-steel hands with a spade on the hour hand and a shadow, a brass cap, the
// date, and a mock pendulum, as such clocks have: a curved slot under the XII in which a
// brass bob swings once every two seconds. The seconds setting adds a thin red second
// hand that ticks. Integer arithmetic (dial.hpp): pixel for pixel the mock's.
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

constexpr int kCx2 = 63, kCy2 = 63;  // the enamel's centre, half pixels
constexpr Rgb kInk{46, 30, 22}, kDot{149, 135, 114}, kDate{117, 103, 86};
constexpr int kTrack2 = 50;  // the minute track's radius, half pixels
struct Numeral {
  const char *text;
  int x, y;
};
constexpr Numeral kNumerals[] = {{"XII", 23, 10}, {"III", 36, 29}, {"VI", 26, 48}, {"IX", 10, 29}};
constexpr dial::Shade kBlued{{30, 42, 96}, 34, {86, 112, 190}, true, {14, 20, 52}};  // a lit edge, a dark edge
constexpr dial::Point kHour[] = {{-256, 64}, {512, 64}, {736, 198}, {960, 19}, {992, 19}};  // stem, spade, point
constexpr dial::Point kMinute[] = {{-320, 70}, {832, 70}, {1472, 32}, {1504, 32}};
constexpr int kSecondLength2 = 49, kSecondTail2 = 14;
constexpr Rgb kSecond{176, 34, 30};
constexpr int kSlotR2 = 23;                          // the slot's radius, half pixels
constexpr int kSlotIn = 392, kSlotOut = 686;         // its band, squared half pixels
constexpr int kSlotCoreIn = 450, kSlotCoreOut = 615; // inside these it is dark, outside its brass lip
constexpr int kSlotHalf10 = 340, kSlotCore10 = 290;  // its half angle, and its dark part's
constexpr int kSwing10 = 240, kPeriodMs = 2000;      // the bob's swing and period
constexpr Rgb kSlotLip{92, 66, 30}, kSlotDark{26, 18, 14};
constexpr uint8_t kShadow = 70;

}  // namespace

void bracket_bob(const Moment &m, int millis, int &x2, int &y2) {
  const int t = (m.second * 1000 + millis) % kPeriodMs;
  const int swing10 = dial::q_round(kSwing10 * dial::sin_q(t * 3600 / kPeriodMs));
  dial::polar_half(kCx2, kCy2, swing10, kSlotR2, x2, y2);
}

void draw_bracket(Frame &frame, const Moment &m, const Options &o, int millis) {
  sprite::blit(frame, sprite::view(assets::kBracketDial), 0, 0);
  for (int i = 0; i < 60; ++i) {
    int x, y;
    dial::polar_px(kCx2, kCy2, i * 60, kTrack2, x, y);
    if (i % 5) {
      frame.set(x, y, kDot);
    } else if (i % 15) {
      frame.set(x, y, kInk);
      frame.set(x + 1, y, kInk);
      frame.set(x - 1, y, kInk);
      frame.set(x, y + 1, kInk);
      frame.set(x, y - 1, kInk);
    }
  }
  const gfx::fonts::Font &font = gfx::fonts::default_font();  // Capital Hill
  for (const Numeral &n : kNumerals) gfx::fonts::draw(frame, font, n.x, n.y, n.text, kInk, 1);
  // the mock pendulum's slot: a band of an arc under the XII, a brass lip around it
  for (int y = kCy2 / 2 - 14; y <= kCy2 / 2; ++y)
    for (int x = kCx2 / 2 - 9; x <= kCx2 / 2 + 10; ++x) {
      const int dx = 2 * x - kCx2, dy = 2 * y - kCy2;
      const int d2 = dx * dx + dy * dy;
      if (dy >= 0 || d2 < kSlotIn || d2 > kSlotOut) continue;
      if (std::abs(dx) * dial::cos_q(kSlotHalf10) > -dy * dial::sin_q(kSlotHalf10)) continue;
      const bool core = d2 >= kSlotCoreIn && d2 <= kSlotCoreOut && std::abs(dx) * dial::cos_q(kSlotCore10) <= -dy * dial::sin_q(kSlotCore10);
      frame.set(x, y, core ? kSlotDark : kSlotLip);
    }
  int bx2, by2;
  bracket_bob(m, millis, bx2, by2);
  dial::disc(frame, bx2, by2, 9, [](int dx, int dy) { return dx + dy < -1 ? Rgb{255, 232, 150} : Rgb{226, 172, 62}; });
  const gfx::fonts::Font *small = gfx::fonts::by_name("everyday-slight");
  if (!small) small = &font;
  const std::string date = date_text(m, o.month_first);
  gfx::fonts::draw(frame, *small, (Frame::width() - gfx::fonts::width(*small, date)) / 2, 40, date, kDate, 1);
  dial::draw_shape(frame, kCx2, kCy2, (m.hour % 12) * 300 + m.minute * 5, dial::profile(kHour), kBlued, kShadow);
  dial::draw_shape(frame, kCx2, kCy2, m.minute * 60, dial::profile(kMinute), kBlued, kShadow);
  if (o.seconds) dial::thin_hand(frame, kCx2, kCy2, m.second * 60, kSecondLength2, kSecondTail2, kSecond);
  dial::disc(frame, kCx2, kCy2, 21, [](int dx, int dy) {
    return dx + dy < -2 ? Rgb{255, 236, 160} : dx + dy > 3 ? Rgb{150, 104, 36} : Rgb{222, 170, 64};
  });
}

}  // namespace p64::widgets::themed

// The words face: a 12x9 grid of letters (assets/clock/words: a 3x5 alphabet, the
// brushed bezel), the ones that spell the time lit ("IT IS HALF PAST TEN AM"), four
// corner dots for the minutes past the five. The grid is p64's own arrangement: the
// minute words on top, the hours in the middle, O'CLOCK and AM/PM at the bottom.
#include <cstring>

#include "clock_assets.hpp"
#include "sprite.hpp"
#include "themed.hpp"

namespace p64::widgets::themed {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kBg{12, 12, 15}, kUnlit{42, 42, 50}, kLit{255, 250, 232};
constexpr int kCols = 12, kRows = 9;
constexpr const char *kGrid[kRows] = {
    "ITBISQHALFAX", "QUARTERXFIVE", "TWENTYKTENTO", "PASTXONETWOK", "THREEFOURSIX",
    "FIVESEVENTEN", "EIGHTNINEXYZ", "ELEVENTWELVE", "OCLOCKQAMPMX",
};
struct Word {
  const char *name;  // "FIVEm" and "TENm" are the minute words
  uint8_t row, col, len;
};
constexpr Word kWords[] = {
    {"IT", 0, 0, 2},      {"IS", 0, 3, 2},     {"HALF", 0, 6, 4},   {"QUARTER", 1, 0, 7}, {"FIVEm", 1, 8, 4},
    {"TWENTY", 2, 0, 6},  {"TENm", 2, 7, 3},   {"TO", 2, 10, 2},    {"PAST", 3, 0, 4},    {"ONE", 3, 5, 3},
    {"TWO", 3, 8, 3},     {"THREE", 4, 0, 5},  {"FOUR", 4, 5, 4},   {"SIX", 4, 9, 3},     {"FIVE", 5, 0, 4},
    {"SEVEN", 5, 4, 5},   {"TEN", 5, 9, 3},    {"EIGHT", 6, 0, 5},  {"NINE", 6, 5, 4},    {"ELEVEN", 7, 0, 6},
    {"TWELVE", 7, 6, 6},  {"OCLOCK", 8, 0, 6}, {"AM", 8, 7, 2},     {"PM", 8, 9, 2},
};
constexpr const char *kHours[12] = {"TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN"};
constexpr const char *kMinutes[12][3] = {
    {nullptr, nullptr, nullptr}, {"FIVEm", "PAST", nullptr},    {"TENm", "PAST", nullptr},    {"QUARTER", "PAST", nullptr},
    {"TWENTY", "PAST", nullptr}, {"TWENTY", "FIVEm", "PAST"},   {"HALF", "PAST", nullptr},    {"TWENTY", "FIVEm", "TO"},
    {"TWENTY", "TO", nullptr},   {"QUARTER", "TO", nullptr},    {"TENm", "TO", nullptr},      {"FIVEm", "TO", nullptr},
};

}  // namespace

bool word_lit(const char *word, int hour, int minute) {
  const int step = minute / 5;
  const int h12 = step < 7 ? hour % 12 : (hour + 1) % 12;
  if (!std::strcmp(word, "IT") || !std::strcmp(word, "IS")) return true;
  if (!std::strcmp(word, hour < 12 ? "AM" : "PM")) return true;
  if (!std::strcmp(word, kHours[h12])) return true;
  if (step == 0) return !std::strcmp(word, "OCLOCK");
  for (const char *w : kMinutes[step])
    if (w && !std::strcmp(word, w)) return true;
  return false;
}

int word_dots(int minute) { return minute % 5; }

void draw_words(Frame &frame, const Moment &m, const Options &) {
  frame.clear(kBg);
  bool lit[kRows][kCols] = {};
  for (const Word &w : kWords) {
    if (!word_lit(w.name, m.hour, m.minute)) continue;
    for (int i = 0; i < w.len; ++i) lit[w.row][w.col + i] = true;
  }
  for (int r = 0; r < kRows; ++r)
    for (int c = 0; c < kCols; ++c)
      sprite::stamp(frame, sprite::cell(assets::kWordsAlphabet, 26, kGrid[r][c] - 'A'), 8 + c * 4, 5 + r * 6,
                    lit[r][c] ? kLit : kUnlit);
  const int dots = word_dots(m.minute);
  constexpr int kDots[4][2] = {{4, 4}, {58, 4}, {58, 58}, {4, 58}};  // clockwise from top-left
  for (int i = 0; i < 4; ++i) frame.fill_rect(kDots[i][0], kDots[i][1], 2, 2, i < dots ? kLit : kUnlit);
  sprite::blit(frame, sprite::view(assets::kWordsBezel), 0, 0);
}

}  // namespace p64::widgets::themed

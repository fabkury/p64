#include "status_screens.hpp"

#include <vector>

#include "p64/gfx/fonts.hpp"

namespace p64::status_screens {
namespace {

using gfx::Frame;
using gfx::Rgb;
using gfx::fonts::Font;

constexpr Rgb kInk{200, 200, 210};
constexpr Rgb kDim{90, 90, 100};
constexpr Rgb kBorder{28, 28, 34};
constexpr Rgb kAddress{120, 200, 255};
// Inside the 1 px frame border (columns and rows 1 and 62) with a 1 px gap.
constexpr int kInnerTop = 3, kInnerBottom = 60, kInnerWidth = 58;

const Font &body() { return gfx::fonts::system_font(); }
const Font &large() { return gfx::fonts::system_large_font(); }

void border(Frame &f, Rgb colour) {
  f.fill_rect(1, 1, 62, 1, colour);
  f.fill_rect(1, 62, 62, 1, colour);
  f.fill_rect(1, 1, 1, 62, colour);
  f.fill_rect(62, 1, 1, 62, colour);
}

std::string sentence(std::string s) {
  if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
  return s;
}

// Splits text into lines no wider than `width` pixels, at spaces, or inside a word that
// is too long on its own (a hostname): after its last '-' or '.' that fits, else anywhere.
std::vector<std::string> wrap(const Font &font, const std::string &text, int width, int scale = 1) {
  std::vector<std::string> lines;
  std::string line, word;
  auto fits = [&](const std::string &s) { return gfx::fonts::width(font, s, scale) <= width; };
  auto flush_word = [&]() {
    if (word.empty()) return;
    const std::string joined = line.empty() ? word : line + " " + word;
    if (fits(joined)) {
      line = joined;
    } else {
      if (!line.empty()) lines.push_back(line);
      line.clear();
      while (!fits(word)) {
        size_t n = word.size();
        while (n > 1 && !fits(word.substr(0, n))) --n;
        for (size_t k = n; k > 1; --k) {
          if (word[k - 1] == '-' || word[k - 1] == '.') {
            n = k;
            break;
          }
        }
        lines.push_back(word.substr(0, n));
        word = word.substr(n);
      }
      line = word;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') {
      flush_word();
    } else {
      word += c;
    }
  }
  flush_word();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// A vertical stack of centred lines, laid out top to bottom and centred as a block
// between the border's rows. A line's height is its cap height, plus the descent when it
// has descenders; `gap` is the space below it.
struct Line {
  const Font *font;
  std::string text;
  Rgb colour;
  int scale = 1;
  int gap = 3;
};

bool descends(const std::string &s) {
  for (char c : s)
    if (c == 'g' || c == 'j' || c == 'p' || c == 'q' || c == 'y' || c == ',' || c == ';') return true;
  return false;
}

int line_height(const Line &l) {
  return descends(l.text) ? gfx::fonts::line_height(*l.font, l.scale) : gfx::fonts::cap_height(*l.font, l.scale);
}

// Adds the wrapped lines of `text` (at most `max_lines`); the gap follows the last one.
void add(std::vector<Line> &lines, const Font &font, const std::string &text, Rgb colour, int gap = 3, int max_lines = 2,
         int scale = 1) {
  std::vector<std::string> parts = wrap(font, text, kInnerWidth, scale);
  if (static_cast<int>(parts.size()) > max_lines) parts.resize(max_lines);
  for (size_t i = 0; i < parts.size(); ++i) {
    lines.push_back(Line{&font, parts[i], colour, scale, i + 1 == parts.size() ? gap : 2});
  }
}

int stack_height(const std::vector<Line> &lines) {
  int total = 0;
  for (size_t i = 0; i < lines.size(); ++i) total += line_height(lines[i]) + (i + 1 < lines.size() ? lines[i].gap : 0);
  return total;
}

// Draws the stack centred in [top, bottom]; returns the row below the last line.
int draw_stack(Frame &f, const std::vector<Line> &lines, int top = kInnerTop, int bottom = kInnerBottom) {
  const int total = stack_height(lines);
  int y = top + (bottom - top + 1 - total) / 2;
  if (y < top) y = top;
  for (const Line &l : lines) {
    gfx::fonts::draw_centred(f, *l.font, y, l.text, l.colour, l.scale);
    y += line_height(l) + l.gap;
  }
  return y;
}

// An IP address on one line, or split after its second dot when it is too wide.
void add_address(std::vector<Line> &lines, const std::string &ip, Rgb colour, int gap = 3) {
  if (gfx::fonts::width(body(), ip, 1) <= kInnerWidth) {
    lines.push_back(Line{&body(), ip, colour, 1, gap});
    return;
  }
  const size_t mid = ip.find('.', ip.find('.') + 1);
  lines.push_back(Line{&body(), ip.substr(0, mid + 1), colour, 1, 2});
  lines.push_back(Line{&body(), ip.substr(mid + 1), colour, 1, gap});
}

}  // namespace

void no_artwork(Frame &frame, const std::string &reason) {
  frame.clear(gfx::kBlack);
  border(frame, kBorder);
  Rgb bar{70, 70, 70};
  if (reason == "no card") {
    bar = Rgb{160, 40, 40};
  } else if (reason == "offline" || reason.rfind("Makapix", 0) == 0 || reason == "downloading") {
    bar = Rgb{40, 80, 160};
  } else if (reason == "needs pairing") {
    bar = Rgb{140, 60, 160};
  } else if (reason == "empty") {
    bar = Rgb{150, 120, 30};
  }
  std::vector<Line> lines;
  lines.push_back(Line{&large(), "No", kInk, 1, 3});
  lines.push_back(Line{&large(), "artwork", kInk, 1, 6});
  add(lines, body(), sentence(reason), kDim);
  draw_stack(frame, lines, kInnerTop, 55);
  frame.fill_rect(20, 58, 24, 2, bar);
}

void black(Frame &frame) { frame.clear(gfx::kBlack); }

void countdown(Frame &frame, int seconds) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{120, 30, 30});
  std::vector<Line> lines;
  add(lines, body(), "Factory reset", kDim, 7);
  if (seconds > 0) {
    lines.push_back(Line{&large(), std::to_string(seconds), Rgb{255, 90, 90}, 3, 0});
  } else {
    lines.push_back(Line{&large(), "Erasing", Rgb{255, 90, 90}, 1, 0});
  }
  draw_stack(frame, lines);
}

void pairing_code(Frame &frame, const std::string &code) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{60, 30, 90});
  const std::string first = code.substr(0, 3), second = code.size() > 3 ? code.substr(3) : "";
  const Rgb gold{255, 220, 120};
  std::vector<Line> lines;
  lines.push_back(Line{&body(), "Pair code", kDim, 1, 5});
  lines.push_back(Line{&large(), first, gold, 2, 4});
  lines.push_back(Line{&large(), second, gold, 2, 0});
  draw_stack(frame, lines);
}

void paired(Frame &frame) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{30, 90, 40});
  std::vector<Line> lines;
  lines.push_back(Line{&large(), "Paired", Rgb{120, 230, 140}, 1, 5});
  lines.push_back(Line{&body(), "Makapix", kDim, 1, 0});
  draw_stack(frame, lines);
}

void connected(Frame &frame, const std::string &hostname, const std::string &ip) {
  frame.clear(gfx::kBlack);
  border(frame, kBorder);
  std::vector<Line> lines;
  lines.push_back(Line{&large(), "Wi-Fi ok", kInk, 1, 7});
  add(lines, body(), hostname + ".local", kDim, 5);
  add_address(lines, ip, kAddress, 0);
  draw_stack(frame, lines);
}

void stream_waiting(Frame &frame, const std::string &hostname, const std::string &ip, int ddp_port, int raw_port) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{30, 60, 90});
  // The hostname goes only when everything fits: the IP and the ports come first.
  for (const bool with_host : {true, false}) {
    std::vector<Line> lines;
    lines.push_back(Line{&large(), "Stream", kInk, 1, 2});
    lines.push_back(Line{&body(), "waiting", kDim, 1, 4});
    if (with_host) add(lines, body(), hostname + ".local", kDim, 2, 1);
    add_address(lines, ip, kAddress, 4);
    lines.push_back(Line{&body(), "DDP " + std::to_string(ddp_port), kDim, 1, 2});
    lines.push_back(Line{&body(), "UDP " + std::to_string(raw_port), kDim, 1, 0});
    if (with_host && stack_height(lines) > kInnerBottom - kInnerTop + 1) continue;
    draw_stack(frame, lines);
    return;
  }
}

void setup_page(Frame &frame, int page, const std::string &ap_ssid, const std::string &ap_ip) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{90, 70, 20});
  std::vector<Line> lines;
  const Rgb amber{255, 200, 90};
  switch (((page % kSetupPages) + kSetupPages) % kSetupPages) {
    case 0:
      lines.push_back(Line{&large(), "Wi-Fi", kInk, 1, 3});
      lines.push_back(Line{&large(), "setup", kInk, 1, 0});
      break;
    case 1:
      lines.push_back(Line{&body(), "Join", kDim, 1, 5});
      add(lines, large(), ap_ssid, amber, 0);
      break;
    default:
      lines.push_back(Line{&body(), "Open", kDim, 1, 5});
      if (gfx::fonts::width(large(), ap_ip, 1) <= kInnerWidth) {
        lines.push_back(Line{&large(), ap_ip, kAddress, 1, 0});
      } else {  // "192.168." over "4.1"
        const size_t mid = ap_ip.find('.', ap_ip.find('.') + 1);
        lines.push_back(Line{&large(), ap_ip.substr(0, mid + 1), kAddress, 1, 3});
        lines.push_back(Line{&large(), ap_ip.substr(mid + 1), kAddress, 1, 0});
      }
      break;
  }
  draw_stack(frame, lines, kInnerTop, 54);
  // Which page: three dots at the bottom, the current one lit.
  for (int i = 0; i < kSetupPages; ++i) {
    frame.fill_rect(26 + i * 5, 58, 2, 2, i == ((page % kSetupPages) + kSetupPages) % kSetupPages ? amber : kDim);
  }
}

void update(Frame &frame, const UpdateView &view) {
  frame.clear(gfx::kBlack);
  std::vector<Line> lines;
  switch (view.phase) {
    case UpdateView::Phase::Downloading:
    case UpdateView::Phase::Verifying: {
      border(frame, Rgb{30, 60, 90});
      lines.push_back(Line{&large(), "Updating", kInk, 1, 4});
      if (!view.version.empty()) add(lines, body(), "to " + view.version, kDim, 4, 1);
      const int bar_top = draw_stack(frame, lines, kInnerTop, 38);
      // The progress bar: an outline and its fill, then the percentage or "verifying".
      const int x = 8, w = 48, h = 6, y = bar_top + 2;
      frame.fill_rect(x, y, w, 1, kDim);
      frame.fill_rect(x, y + h - 1, w, 1, kDim);
      frame.fill_rect(x, y, 1, h, kDim);
      frame.fill_rect(x + w - 1, y, 1, h, kDim);
      const bool verifying = view.phase == UpdateView::Phase::Verifying;
      const int pct = verifying ? 100 : (view.percent < 0 ? 0 : view.percent > 100 ? 100 : view.percent);
      frame.fill_rect(x + 2, y + 2, (w - 4) * pct / 100, h - 4, kAddress);
      const std::string note = verifying ? "verifying" : view.percent < 0 ? "starting" : std::to_string(pct) + " %";
      gfx::fonts::draw_centred(frame, body(), y + h + 4, note, kDim, 1);
      break;
    }
    case UpdateView::Phase::Ready:
      border(frame, Rgb{30, 90, 40});
      lines.push_back(Line{&large(), "Update", Rgb{120, 230, 140}, 1, 3});
      lines.push_back(Line{&large(), "ready", Rgb{120, 230, 140}, 1, 7});
      lines.push_back(Line{&body(), "Restart to", kDim, 1, 2});
      lines.push_back(Line{&body(), "run it", kDim, 1, 0});
      draw_stack(frame, lines);
      break;
    case UpdateView::Phase::Failed:
      border(frame, Rgb{120, 30, 30});
      lines.push_back(Line{&large(), "Update", Rgb{255, 90, 90}, 1, 3});
      lines.push_back(Line{&large(), "failed", Rgb{255, 90, 90}, 1, 7});
      add(lines, body(), sentence(view.error), kDim, 0, 2);
      draw_stack(frame, lines);
      break;
  }
}

}  // namespace p64::status_screens

#include "status_screens.hpp"

#include <vector>

#include "p64/gfx/text.hpp"

namespace p64::status_screens {
namespace {

using gfx::Frame;
using gfx::Rgb;

constexpr Rgb kInk{200, 200, 210};
constexpr Rgb kDim{90, 90, 100};
constexpr Rgb kBorder{28, 28, 34};

void border(Frame &f, Rgb colour) {
  f.fill_rect(1, 1, 62, 1, colour);
  f.fill_rect(1, 62, 62, 1, colour);
  f.fill_rect(1, 1, 1, 62, colour);
  f.fill_rect(62, 1, 1, 62, colour);
}

// Splits text into lines that fit the panel at scale 1 with the given spacing.
std::vector<std::string> wrap(const std::string &text, int spacing) {
  const int per_line = (Frame::width() - 2) / (gfx::text::kGlyphWidth + spacing);
  std::vector<std::string> lines;
  std::string word, line;
  auto flush_word = [&]() {
    if (word.empty()) return;
    if (!line.empty() && static_cast<int>(line.size() + 1 + word.size()) > per_line) {
      lines.push_back(line);
      line.clear();
    }
    if (!line.empty()) line += ' ';
    while (static_cast<int>(word.size()) > per_line) {
      lines.push_back(line + word.substr(0, per_line - line.size()));
      word = word.substr(per_line - line.size());
      line.clear();
    }
    line += word;
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

void lines_centred(Frame &f, int y, const std::vector<std::string> &lines, Rgb colour, int scale, int spacing) {
  const int step = (gfx::text::kGlyphHeight + 2) * scale;
  for (const std::string &l : lines) {
    gfx::text::draw_centred(f, y, l, colour, scale, spacing);
    y += step;
  }
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
  gfx::text::draw_centred(frame, 10, "NO", kInk, 2, 1);
  gfx::text::draw_centred(frame, 30, "ARTWORK", kInk, 1, 1);
  std::vector<std::string> lines = wrap(reason, 1);
  if (lines.size() > 2) lines.resize(2);
  lines_centred(frame, 42, lines, kDim, 1, 1);
  frame.fill_rect(20, 60, 24, 2, bar);
}

void black(Frame &frame) { frame.clear(gfx::kBlack); }

void countdown(Frame &frame, int seconds) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{120, 30, 30});
  gfx::text::draw_centred(frame, 6, "FACTORY", kDim, 1, 1);
  gfx::text::draw_centred(frame, 15, "RESET", kDim, 1, 1);
  if (seconds > 0) {
    gfx::text::draw_centred(frame, 28, std::to_string(seconds), Rgb{255, 90, 90}, 4, 1);
  } else {
    gfx::text::draw_centred(frame, 32, "ERASING", Rgb{255, 90, 90}, 1, 1);
  }
}

void pairing_code(Frame &frame, const std::string &code) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{60, 30, 90});
  gfx::text::draw_centred(frame, 5, "MAKAPIX", kDim, 1, 1);
  gfx::text::draw_centred(frame, 14, "PAIR CODE", kDim, 1, 1);
  const std::string first = code.substr(0, 3), second = code.size() > 3 ? code.substr(3) : "";
  gfx::text::draw_centred(frame, 26, first, Rgb{255, 220, 120}, 2, 2);
  gfx::text::draw_centred(frame, 44, second, Rgb{255, 220, 120}, 2, 2);
}

void paired(Frame &frame) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{30, 90, 40});
  gfx::text::draw_centred(frame, 20, "PAIRED", Rgb{120, 230, 140}, 2, 1);
  gfx::text::draw_centred(frame, 40, "MAKAPIX", kDim, 1, 1);
}

void connected(Frame &frame, const std::string &hostname, const std::string &ip) {
  frame.clear(gfx::kBlack);
  border(frame, kBorder);
  gfx::text::draw_centred(frame, 6, "WI-FI OK", kInk, 1, 1);
  std::vector<std::string> host = wrap(hostname + ".local", 0);
  if (host.size() > 2) host.resize(2);
  lines_centred(frame, 18, host, kDim, 1, 0);
  // The IP on two lines when it does not fit one (15 characters at 5 px = 75 px).
  std::vector<std::string> lines;
  if (gfx::text::text_width(ip, 1, 0) <= Frame::width() - 2) {
    lines.push_back(ip);
  } else {
    const size_t mid = ip.find('.', ip.find('.') + 1);
    lines.push_back(ip.substr(0, mid + 1));
    lines.push_back(ip.substr(mid + 1));
  }
  lines_centred(frame, 40, lines, Rgb{120, 200, 255}, 1, 0);
}

void stream_waiting(Frame &frame, const std::string &hostname, const std::string &ip, int ddp_port, int raw_port) {
  frame.clear(gfx::kBlack);
  border(frame, Rgb{30, 60, 90});
  gfx::text::draw_centred(frame, 4, "STREAM", kInk, 1, 1);
  gfx::text::draw_centred(frame, 12, "WAITING", kDim, 1, 1);
  std::vector<std::string> host = wrap(hostname + ".local", 0);
  if (host.size() > 1) host.resize(1);
  lines_centred(frame, 24, host, kDim, 1, 0);
  std::vector<std::string> lines;
  if (gfx::text::text_width(ip, 1, 0) <= Frame::width() - 2) {
    lines.push_back(ip);
  } else {
    const size_t mid = ip.find('.', ip.find('.') + 1);
    lines.push_back(ip.substr(0, mid + 1));
    lines.push_back(ip.substr(mid + 1));
  }
  lines_centred(frame, 34, lines, Rgb{120, 200, 255}, 1, 0);
  gfx::text::draw_centred(frame, 52, "DDP " + std::to_string(ddp_port), kDim, 1, 0);
  gfx::text::draw_centred(frame, 58, "UDP " + std::to_string(raw_port), kDim, 1, 0);
}

}  // namespace p64::status_screens

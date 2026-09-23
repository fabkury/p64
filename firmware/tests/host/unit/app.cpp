// Host unit tests: code that was already free of ESP-IDF but never compiled on the PC
// until the review of 2026-09-22 (P-T2): the PNG encoder behind the live preview, the
// time zone table, the local folder index, Artwork (decoder + scaler), the status
// screens and the boot animation.
#include "common.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;

int lit(const Frame &f) {
  int n = 0;
  for (int y = 0; y < Frame::height(); ++y)
    for (int x = 0; x < Frame::width(); ++x) {
      const Rgb c = f.get(x, y);
      if (c.r || c.g || c.b) ++n;
    }
  return n;
}

std::vector<uint8_t> read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string corpus(const char *name) {
  return std::string(P64_HOST_TESTS_DIR) + "/corpus/" + name;
}

TEST_CASE("png_encode: the live preview's PNG decodes back pixel for pixel") {
  std::vector<uint8_t> rgb(64 * 48 * 3);
  for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<uint8_t>(i * 7 + i / 3);
  const std::vector<uint8_t> png = p64::gfx::encode_png_rgb(rgb.data(), 64, 48);
  CHECK(p64::decode::sniff(png.data(), png.size()) == p64::decode::Format::Png);
  auto dec = p64::decode::create(p64::decode::Format::Png);
  REQUIRE(dec);
  REQUIRE(dec->open(png.data(), png.size(), Rgb{0, 0, 0}));
  CHECK_EQ(dec->info().width, 64);
  CHECK_EQ(dec->info().height, 48);
  uint32_t delay = 0;
  REQUIRE(dec->next(delay));
  CHECK(std::memcmp(dec->canvas(), rgb.data(), rgb.size()) == 0);
}

TEST_CASE("tz: IANA names resolve to POSIX rules; rules pass through") {
  CHECK(std::string(p64::net::tz::posix_for("America/New_York")) == "EST5EDT,M3.2.0,M11.1.0");
  CHECK(std::string(p64::net::tz::posix_for("UTC")).rfind("UTC", 0) == 0);
  CHECK(std::string(p64::net::tz::posix_for("EST5EDT,M3.2.0,M11.1.0")) == "EST5EDT,M3.2.0,M11.1.0");
  // Known limitation (found 2026-09-22): a passed-through rule may not contain '/', so a
  // POSIX rule with a transition time such as "CET-1CEST,M3.5.0,M10.5.0/3" is refused.
  // The IANA names cover those zones.
  CHECK(p64::net::tz::posix_for("CET-1CEST,M3.5.0,M10.5.0/3") == nullptr);
  CHECK(p64::net::tz::posix_for("Nowhere/Special") == nullptr);
  CHECK(p64::net::tz::count() > 400);
  for (size_t i = 1; i < p64::net::tz::count(); ++i)  // the binary search needs the order
    CHECK(std::strcmp(p64::net::tz::name_at(i - 1), p64::net::tz::name_at(i)) < 0);
}

TEST_CASE("local_index: artworks only, newest first, the cap counted as skipped") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "p64_local_index_test";
  fs::remove_all(dir);
  fs::create_directories(dir / "sub");
  fs::create_directories(dir / ".hidden");
  const char *names[] = {"a.gif", "b.PNG", "c.webp", "d.bmp", "notes.txt", "e.jpg"};
  int age = 0;
  for (const char *n : names) {
    std::ofstream(dir / n) << "x";
    fs::last_write_time(dir / n, fs::file_time_type::clock::now() - std::chrono::hours(24 * ++age));
  }
  CHECK(p64::content::artwork_extension("x.gif"));
  CHECK(p64::content::artwork_extension("x.WebP"));
  CHECK(!p64::content::artwork_extension("x.txt"));
  p64::content::LocalEntries out;
  std::string error;
  uint32_t skipped = 99;
  REQUIRE(p64::content::scan_folder(dir.string(), 100, out, error, &skipped));
  REQUIRE_EQ(out.size(), 4u);  // gif, png, webp, bmp
  CHECK(std::string(out[0].name) == "a.gif");  // the newest
  CHECK(std::string(out[3].name) == "d.bmp");
  for (size_t i = 1; i < out.size(); ++i) CHECK(out[i - 1].mtime >= out[i].mtime);
  CHECK_EQ(skipped, 0u);
  REQUIRE(p64::content::scan_folder(dir.string(), 2, out, error, &skipped));
  CHECK_EQ(out.size(), 2u);
  CHECK_EQ(skipped, 2u);
  std::vector<std::string> subs;
  REQUIRE(p64::content::list_subfolders(dir.string(), subs, error));
  REQUIRE_EQ(subs.size(), 1u);
  CHECK(subs[0] == "sub");
  CHECK(!p64::content::scan_folder((dir / "absent").string(), 10, out, error, &skipped));
  fs::remove_all(dir);
}

TEST_CASE("artwork: a GIF plays through decoder and scaler; garbage is refused") {
  p64::playback::Artwork art;
  std::string error;
  REQUIRE(art.open(read_file(corpus("gif_anim_32.gif")), "gif_anim_32.gif", Rgb{0, 0, 0}, error));
  CHECK(art.format() == p64::decode::Format::Gif);
  Frame f;
  uint32_t delay = 0;
  REQUIRE(art.next_frame(f, delay));
  CHECK(delay >= 11);  // the browser rule never leaves a delay at or below 10 ms
  CHECK(lit(f) > 0);
  CHECK(!art.is_static());
  p64::playback::Artwork bad;
  CHECK(!bad.open(std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}, "junk.gif", Rgb{0, 0, 0}, error));
  CHECK(!error.empty());
}

// Every status screen keeps its text inside the 1 px frame border with a 1 px gap: the
// outer ring and the ring just inside the border stay black (spec 6.4; the screens moved
// to Everyday Standard and Ample on 2026-09-23, where a long line ran into the border).
int lit_outside_inner(const Frame &f) {
  int n = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      const bool outer = x == 0 || x == 63 || y == 0 || y == 63;
      const bool inside_border = x >= 2 && x <= 61 && y >= 2 && y <= 61;
      const bool gap = inside_border && (x == 2 || x == 61 || y == 2 || y == 61);
      if ((outer || gap) && !(f.get(x, y) == Rgb{0, 0, 0})) ++n;
    }
  return n;
}

TEST_CASE("status screens stay inside their border, long names and addresses included") {
  namespace ss = p64::status_screens;
  Frame f;
  auto check_screen = [&](const char *what) {
    CAPTURE(what);
    CHECK(lit(f) > 20);
    CHECK_EQ(lit_outside_inner(f), 0);
  };
  for (const char *r : {"no card", "offline", "needs pairing", "empty", "Makapix: no artworks yet", "downloading"}) {
    ss::no_artwork(f, r);
    check_screen(r);
  }
  ss::countdown(f, 3);
  check_screen("countdown 3");
  ss::countdown(f, 0);
  check_screen("erasing");
  ss::pairing_code(f, "TDPCHB");
  check_screen("pairing");
  ss::paired(f);
  check_screen("paired");
  for (const char *host : {"p64", "p64-living-room"}) {
    for (const char *ip : {"192.168.4.92", "192.168.100.200"}) {
      ss::connected(f, host, ip);
      check_screen("connected");
      ss::stream_waiting(f, host, ip, 4048, 4064);
      check_screen("stream waiting");
    }
  }
  for (int page = 0; page < ss::kSetupPages; ++page) {
    ss::setup_page(f, page, "p64-setup", "192.168.4.1");
    check_screen("setup");
  }
  Frame other;
  ss::setup_page(other, 1, "p64-setup", "192.168.4.1");
  ss::setup_page(f, 2, "p64-setup", "192.168.4.1");
  CHECK(std::memcmp(f.data(), other.data(), Frame::bytes()) != 0);  // the pages differ
  ss::UpdateView v;
  v.version = "0.2.0";
  for (const int pct : {-1, 0, 42, 100}) {
    v.percent = pct;
    ss::update(f, v);
    check_screen("updating");
  }
  // The bar fills with the percentage.
  v.percent = 10;
  ss::update(other, v);
  v.percent = 90;
  ss::update(f, v);
  CHECK(lit(f) > lit(other));
  v.phase = ss::UpdateView::Phase::Verifying;
  ss::update(f, v);
  check_screen("verifying");
  v.phase = ss::UpdateView::Phase::Ready;
  ss::update(f, v);
  check_screen("ready");
  v.phase = ss::UpdateView::Phase::Failed;
  v.error = "download failed: HTTP 404 from the release server";
  ss::update(f, v);
  check_screen("failed");
}

TEST_CASE("status screens and the boot animation draw something") {
  Frame f;
  p64::status_screens::no_artwork(f, "no card");
  CHECK(lit(f) > 20);
  p64::status_screens::pairing_code(f, "TDPCHB");
  CHECK(lit(f) > 20);
  p64::status_screens::countdown(f, 3);
  CHECK(lit(f) > 5);
  p64::status_screens::black(f);
  CHECK_EQ(lit(f), 0);
  p64::BootAnimation boot;
  bool running = boot.render(f, 500, 2000);
  CHECK(running);
  CHECK(lit(f) > 0);
  CHECK(!boot.render(f, 2500, 2000));  // past its duration
}

}  // namespace

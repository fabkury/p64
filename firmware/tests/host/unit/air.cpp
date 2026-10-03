// Host unit tests: the air widget (spec 7.4): the Open-Meteo air quality reply, the index
// bands, the graph's scale, and the face compared pixel for pixel with the references
// tools/mock_air_widget.py writes (tests/host/corpus/air/).
#include "common.hpp"
#include "faces.hpp"

#include <sstream>

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;
using namespace p64::widgets::air_model;
namespace faces = p64::widgets::faces;

// Open-Meteo's reply for New York on 2026-10-03, cut to the first hours of each series
// (the full one is 48); the second hour's US index is missing.
const char kReply[] = R"({"latitude":40.699997,"longitude":-74.0,"utc_offset_seconds":-14400,
  "timezone":"America/New_York","current":{"time":"2026-10-03T16:00","interval":3600,"us_aqi":44,
  "european_aqi":38,"pm2_5":5.7,"pm10":6.4,"uv_index":1.40},
  "hourly":{"time":["2026-10-03T00:00","2026-10-03T01:00","2026-10-03T02:00"],
  "us_aqi":[55,null,54],"european_aqi":[29,24,20],"uv_index":[0.00,0.45,5.35]}})";

std::string corpus(const std::string &name) { return std::string(P64_HOST_TESTS_DIR) + "/corpus/air/" + name; }

TEST_CASE("air_model: the request asks for both indexes, the particulates, UV and two days of hours") {
  const std::string url = request_url(40.7128f, -74.006f);
  CHECK(url.find("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=40.7128&longitude=-74.0060") == 0);
  CHECK(url.find("current=us_aqi,european_aqi,pm2_5,pm10,uv_index") != std::string::npos);
  CHECK(url.find("hourly=us_aqi,european_aqi,uv_index") != std::string::npos);
  CHECK(url.find("timezone=auto&forecast_days=2") != std::string::npos);
}

TEST_CASE("weather_model: a fetch is due at once for a new request, else after the pause and the refresh") {
  using p64::widgets::weather_model::fetch_due;
  const int64_t minute = 60 * 1000000LL;
  CHECK(fetch_due(false, 0, 0, 5, 30));                           // never fetched
  CHECK(!fetch_due(false, 0, 5 * minute, 4 * minute, 30));        // a failure's retry pause
  CHECK(fetch_due(false, 0, 5 * minute, 5 * minute, 30));
  CHECK(!fetch_due(false, 10 * minute, 40 * minute, 39 * minute, 30));  // fresh
  CHECK(fetch_due(false, 10 * minute, 40 * minute, 40 * minute, 30));   // refresh_minutes old
  CHECK(!fetch_due(false, 10 * minute, 50 * minute, 45 * minute, 30));  // old, but pausing after a failure
  // Another location or other units: now, whatever the age or the pause (2026-10-03: a
  // new location showed the old place's numbers until the next refresh).
  CHECK(fetch_due(true, 10 * minute, 40 * minute, 11 * minute, 30));
  CHECK(fetch_due(true, 0, 5 * minute, 1 * minute, 30));
}

TEST_CASE("air_model: the reply parses, missing values stay missing, a bad reply says why") {
  Air a;
  std::string error;
  REQUIRE(parse(kReply, sizeof(kReply) - 1, a, error));
  CHECK(a.valid);
  CHECK_EQ(a.us_aqi, 44);
  CHECK_EQ(a.eu_aqi, 38);
  CHECK(a.uv == doctest::Approx(1.4));
  CHECK(a.pm2_5 == doctest::Approx(5.7));
  CHECK(a.pm10 == doctest::Approx(6.4));
  CHECK_EQ(a.utc_offset_s, -14400);
  CHECK_EQ(a.hours, 3);
  CHECK_EQ(a.us_hours[0], 55);
  CHECK_EQ(a.us_hours[1], -1);  // null
  CHECK_EQ(a.us_hours[2], 54);
  CHECK_EQ(a.eu_hours[2], 20);
  CHECK(a.uv_hours[2] == doctest::Approx(5.35));
  CHECK_EQ(a.us_hours[3], -1);  // beyond the reply
  CHECK(a.uv_hours[3] < 0);
  // 2026-10-03T00:00 on the location's clock: 20729 days after 1970-01-01.
  CHECK_EQ(a.first_hour_s, 20729LL * 86400);

  const char no_current[] = R"({"hourly":{"time":["2026-10-03T00:00"]}})";
  CHECK(!parse(no_current, sizeof(no_current) - 1, a, error));
  CHECK(!a.valid);
  const char refused[] = R"({"error":true,"reason":"Latitude must be in range of -90 to 90"})";
  CHECK(!parse(refused, sizeof(refused) - 1, a, error));
  CHECK(error == "Latitude must be in range of -90 to 90");
  CHECK(!parse("<html>", 6, a, error));
  CHECK(error == "not JSON");
  // A current value that is null is missing, not zero.
  const char nulls[] = R"({"utc_offset_seconds":0,"current":{"us_aqi":null,"uv_index":null},
    "hourly":{"time":["2026-10-03T00:00"]}})";
  REQUIRE(parse(nulls, sizeof(nulls) - 1, a, error));
  CHECK_EQ(a.us_aqi, -1);
  CHECK(a.uv < 0);
  CHECK(a.pm2_5 < 0);
}

TEST_CASE("air_model: local times and the hour of now at the location") {
  int64_t s = 0;
  CHECK(local_seconds("1970-01-01T00:00", s));
  CHECK_EQ(s, 0);
  CHECK(local_seconds("2024-02-29T23:30", s));  // a leap day
  CHECK_EQ(s, 19782LL * 86400 + 23 * 3600 + 30 * 60);
  CHECK(!local_seconds("2026-10-03", s));
  CHECK(!local_seconds("2026-13-03T00:00", s));
  CHECK(!local_seconds(nullptr, s));

  Air a;
  a.first_hour_s = 20729LL * 86400;  // 2026-10-03T00:00 local
  a.utc_offset_s = -14400;           // New York in summer
  const int64_t utc_midnight = 20729LL * 86400;
  CHECK_EQ(hour_index(a, utc_midnight + 4 * 3600), 0);          // 04:00 UTC is local midnight
  CHECK_EQ(hour_index(a, utc_midnight + 20 * 3600 + 59 * 60), 16);
  CHECK_EQ(hour_index(a, utc_midnight + 28 * 3600), 24);        // the next local day: the second half of the series
  CHECK_EQ(hour_index(a, utc_midnight + 3 * 3600), -1);         // still yesterday there
}

TEST_CASE("air_model: the bands of the three scales and their words") {
  CHECK(std::string(band(Scale::UsAqi, 0).word) == "GOOD");
  CHECK(std::string(band(Scale::UsAqi, 50).word) == "GOOD");
  CHECK(std::string(band(Scale::UsAqi, 51).word) == "MODERATE");
  CHECK(std::string(band(Scale::UsAqi, 150).word) == "SENSITIVE");
  CHECK(std::string(band(Scale::UsAqi, 200).word) == "UNHEALTHY");
  CHECK(std::string(band(Scale::UsAqi, 300).word) == "V.UNHEALTHY");
  CHECK(std::string(band(Scale::UsAqi, 301).word) == "HAZARDOUS");
  CHECK(std::string(band(Scale::UsAqi, 5000).word) == "HAZARDOUS");
  CHECK(std::string(band(Scale::EuropeanAqi, 20).word) == "GOOD");
  CHECK(std::string(band(Scale::EuropeanAqi, 21).word) == "FAIR");
  CHECK(std::string(band(Scale::EuropeanAqi, 100).word) == "VERY POOR");
  CHECK(std::string(band(Scale::EuropeanAqi, 101).word) == "EXTREME");
  CHECK(std::string(band(Scale::Uv, 2).word) == "LOW");
  CHECK(std::string(band(Scale::Uv, 3).word) == "MODERATE");
  CHECK(std::string(band(Scale::Uv, 7).word) == "HIGH");
  CHECK(std::string(band(Scale::Uv, 10).word) == "VERY HIGH");
  CHECK(std::string(band(Scale::Uv, 11).word) == "EXTREME");
  CHECK(band(Scale::UsAqi, 30).colour == Rgb{0, 228, 0});
}

TEST_CASE("air_model: the graph's scale is the peak's band, never less than the second band") {
  CHECK_EQ(scale_top(Scale::UsAqi, 0), 100);
  CHECK_EQ(scale_top(Scale::UsAqi, 55), 100);
  CHECK_EQ(scale_top(Scale::UsAqi, 100), 100);
  CHECK_EQ(scale_top(Scale::UsAqi, 101), 150);
  CHECK_EQ(scale_top(Scale::UsAqi, 260), 300);
  CHECK_EQ(scale_top(Scale::UsAqi, 301), 400);  // the open band: one more band's width at least
  CHECK_EQ(scale_top(Scale::UsAqi, 480), 480);  // then the peak itself
  CHECK_EQ(scale_top(Scale::EuropeanAqi, 10), 40);
  CHECK_EQ(scale_top(Scale::EuropeanAqi, 64), 80);
  CHECK_EQ(scale_top(Scale::EuropeanAqi, 130), 130);
}

// A state as the mock wrote it (corpus/air/<name>.txt): "key values" lines.
struct State {
  p64::system::Settings settings;
  Air air;
  std::string error;
  int64_t now = 0, utc_now_s = 0;
};

State load_state(const std::string &name) {
  State st;
  std::ifstream in(corpus(name + ".txt"));
  REQUIRE(in.good());
  Air &a = st.air;
  for (int i = 0; i < kMaxHours; ++i) {
    a.us_hours[i] = a.eu_hours[i] = -1;
    a.uv_hours[i] = -1;
  }
  a.fetched_us = 1000;
  a.first_hour_s = 20729LL * 86400;
  a.hours = 24;
  int aqi = -1, hour = 0, age_min = 0;
  int16_t hours[24] = {};
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key == "european") {
      ls >> st.settings.air.european;
    } else if (key == "location_set") {
      ls >> st.settings.weather.location_set;
    } else if (key == "age_min") {
      ls >> age_min;
    } else if (key == "error") {
      ls >> st.error;
      if (st.error == "-") st.error.clear();
      for (char &c : st.error) c = c == '_' ? ' ' : c;
    } else if (key == "valid") {
      ls >> a.valid;
    } else if (key == "hour") {
      ls >> hour;
    } else if (key == "aqi") {
      ls >> aqi;
    } else if (key == "uv") {
      ls >> a.uv;
    } else if (key == "pm2_5") {
      ls >> a.pm2_5;
    } else if (key == "pm10") {
      ls >> a.pm10;
    } else if (key == "aqi_hours") {
      for (int16_t &v : hours) ls >> v;
    } else if (key == "uv_hours") {
      for (int i = 0; i < 24; ++i) ls >> a.uv_hours[i];
    }
  }
  // The state's index goes where the settings look for it; the other stays missing.
  (st.settings.air.european ? a.eu_aqi : a.us_aqi) = aqi;
  for (int i = 0; i < 24; ++i) (st.settings.air.european ? a.eu_hours : a.us_hours)[i] = hours[i];
  st.now = a.fetched_us + static_cast<int64_t>(age_min) * 60 * 1000000;
  st.utc_now_s = a.first_hour_s + hour * 3600 + 120;
  return st;
}

std::vector<uint8_t> read_ppm(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::string magic;
  int w = 0, h = 0, max = 0;
  in >> magic >> w >> h >> max;
  in.get();
  REQUIRE(magic == "P6");
  REQUIRE_EQ(w, Frame::width());
  REQUIRE_EQ(h, Frame::height());
  std::vector<uint8_t> px(Frame::bytes());
  in.read(reinterpret_cast<char *>(px.data()), static_cast<std::streamsize>(px.size()));
  REQUIRE(in.gcount() == static_cast<std::streamsize>(px.size()));
  return px;
}

TEST_CASE("faces: the air widget matches the mock's references pixel for pixel") {
  for (const char *name : {"good", "moderate", "sensitive", "unhealthy", "very-unhealthy", "hazardous", "european", "old",
                           "no-location", "no-data"}) {
    CAPTURE(name);
    const State st = load_state(name);
    Frame f;
    faces::draw_air(f, st.settings, st.air, st.error, st.now, st.utc_now_s);
    const std::vector<uint8_t> ref = read_ppm(corpus(std::string(name) + ".ppm"));
    int differing = 0, first_x = -1, first_y = -1;
    for (int y = 0; y < Frame::height(); ++y) {
      for (int x = 0; x < Frame::width(); ++x) {
        const uint8_t *p = &ref[static_cast<size_t>(y * Frame::width() + x) * 3];
        if (!(f.get(x, y) == Rgb{p[0], p[1], p[2]})) {
          if (!differing++) first_x = x, first_y = y;
        }
      }
    }
    CAPTURE(first_x);
    CAPTURE(first_y);
    CHECK_EQ(differing, 0);
  }
}

TEST_CASE("faces: the air widget turns stale after 6 h, survives midnight and missing values") {
  State st = load_state("good");
  Frame fresh, stale;
  faces::draw_air(fresh, st.settings, st.air, "", st.now, st.utc_now_s);
  faces::draw_air(stale, st.settings, st.air, "", st.air.fetched_us + faces::kWeatherStaleUs + 1, st.utc_now_s);
  int graph = 0;
  for (int y = 43; y < 64; ++y)
    for (int x = 0; x < 64; ++x) graph += !(stale.get(x, y) == Rgb{0, 0, 0});
  CHECK_EQ(graph, 0);  // "AIR / NO DATA": nothing where the graph was
  CHECK(!(fresh.get(40, 62) == stale.get(40, 62)));  // the mark of 16:00 was there

  // After the location's midnight the second day of the series is drawn: here it is
  // empty, so no bar, and the hour mark is at 01:00.
  st.air.hours = 48;
  Frame next;
  faces::draw_air(next, st.settings, st.air, "", st.now, st.air.first_hour_s + 25 * 3600);
  CHECK(next.get(8 + 2, 62) == Rgb{255, 255, 255});
  int bars = 0;
  for (int y = 43; y <= 60; ++y)
    for (int x = 0; x < 64; ++x) bars += !(next.get(x, y) == Rgb{0, 0, 0});
  CHECK_EQ(bars, 0);
  // A series that ended (the device kept old data past its second midnight): the
  // numbers stay, the graph is left empty rather than read past the end.
  faces::draw_air(next, st.settings, st.air, "", st.now, st.air.first_hour_s + 49 * 3600);
  CHECK(next.get(8 + 2, 62) == Rgb{255, 255, 255});
  // The clock unknown: no hour mark, the first day's bars all dimmed.
  Frame unknown;
  faces::draw_air(unknown, st.settings, st.air, "", st.now, 0);
  CHECK(unknown.get(40, 62) == Rgb{0, 0, 0});
  CHECK(!(unknown.get(40, 60) == Rgb{0, 0, 0}));
  // Missing current values draw "--" instead of a number, without a word.
  st.air.us_aqi = -1;
  st.air.uv = -1;
  st.air.pm2_5 = -1;
  Frame missing;
  faces::draw_air(missing, st.settings, st.air, "", st.now, st.utc_now_s);
  CHECK(!(missing.get(2, 6) == fresh.get(2, 6)));
}

}  // namespace

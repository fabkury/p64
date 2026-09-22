// Host unit tests: the PIN's rules (p64_web/src/auth_rules.cpp, spec 10.3).
#include "common.hpp"
#include "auth_rules.hpp"

#include <set>

namespace {

namespace rules = p64::web::auth::rules;
constexpr int64_t kS = 1000000;

TEST_CASE("pin: 4 to 8 digits") {
  CHECK(rules::valid_pin("1234"));
  CHECK(rules::valid_pin("12345678"));
  CHECK(!rules::valid_pin("123"));
  CHECK(!rules::valid_pin("123456789"));
  CHECK(!rules::valid_pin("12a4"));
  CHECK(!rules::valid_pin(""));
  CHECK(!rules::valid_pin("12 34"));
}

TEST_CASE("pin: five wrong PINs lock for 30 s; a right one before that resets the count") {
  rules::Lockout l;
  for (int i = 0; i < 4; ++i) CHECK(!l.attempt(false, i * kS));
  CHECK(l.attempt(true, 5 * kS));  // four wrong, then right: accepted, count reset
  bool locked = false;
  for (int i = 0; i < 4; ++i) {
    CHECK(!l.attempt(false, 6 * kS, &locked));
    CHECK(!locked);
  }
  CHECK(!l.attempt(false, 6 * kS, &locked));  // the fifth
  CHECK(locked);
  CHECK_EQ(l.locked_seconds(6 * kS), 30);
  CHECK_EQ(l.locked_seconds(6 * kS + 1), 30);  // rounded up for Retry-After
  CHECK(!l.attempt(true, 20 * kS));             // even the right PIN waits out the lockout
  CHECK_EQ(l.locked_seconds(35 * kS + 500000), 1);
  CHECK_EQ(l.locked_seconds(36 * kS), 0);
  CHECK(l.attempt(true, 36 * kS));  // open again
}

TEST_CASE("pin: attempts during a lockout do not extend it or count") {
  rules::Lockout l;
  for (int i = 0; i < 5; ++i) l.attempt(false, 0);
  for (int i = 0; i < 20; ++i) CHECK(!l.attempt(false, 10 * kS));
  CHECK_EQ(l.locked_seconds(29 * kS), 1);
  CHECK(!l.attempt(false, 30 * kS));  // the first wrong PIN after the lockout: 1 of 5, not a new lock
  CHECK_EQ(l.locked_seconds(30 * kS), 0);
}

TEST_CASE("pin: sessions, eviction of the least recently seen, sign-out") {
  rules::Sessions s;
  uint8_t raw[16] = {};
  std::vector<std::string> tokens;
  for (int i = 0; i < 8; ++i) {
    raw[0] = static_cast<uint8_t>(i);
    tokens.push_back(s.create(raw, i * kS));
  }
  CHECK_EQ(s.count(), 8);
  CHECK_EQ(tokens[0].size(), 32u);
  CHECK(tokens[0].rfind("00000000", 0) == 0);
  CHECK(s.touch(tokens[0].c_str(), 100 * kS));  // the oldest is seen again...
  raw[0] = 0xAB;
  const std::string ninth = s.create(raw, 101 * kS);  // ...so the ninth evicts the second
  CHECK_EQ(s.count(), 8);
  CHECK(s.touch(tokens[0].c_str(), 102 * kS));
  CHECK(!s.touch(tokens[1].c_str(), 102 * kS));
  CHECK(s.touch(ninth.c_str(), 102 * kS));
  s.end(tokens[2].c_str());
  CHECK(!s.touch(tokens[2].c_str(), 103 * kS));
  CHECK_EQ(s.count(), 7);
  CHECK(!s.touch("", 0));
  CHECK(!s.touch("not-a-token", 0));
  s.end_all();  // a PIN change signs every browser out
  CHECK_EQ(s.count(), 0);
  CHECK(!s.touch(ninth.c_str(), 104 * kS));
}

}  // namespace

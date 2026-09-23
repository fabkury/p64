// Host unit tests: the rules of trusted time (p64_net/src/time_rules.cpp, ADR 0011).
#include "common.hpp"

namespace {

namespace tr = p64::net::time_rules;
using tr::Want;

tr::Slots slots(std::initializer_list<tr::Slot> list) {
  tr::Slots s;
  size_t i = 0;
  for (const tr::Slot &x : list) s[i++] = x;
  return s;
}

const tr::Slot kDhcp{"", true};  // set by address: what DHCP option 42 writes
const tr::Slot kEmpty{"", false};
tr::Slot named(const char *name, bool resolved = false) { return tr::Slot{name, resolved}; }

TEST_CASE("time: the build date parses as its midnight UTC") {
  int64_t e = 0;
  REQUIRE(tr::parse_build_date("Sep 22 2026", e));
  CHECK_EQ(e, 1790035200);  // 2026-09-22T00:00:00Z
  REQUIRE(tr::parse_build_date("Jan  1 2026", e));  // __DATE__ pads the day with a space
  CHECK_EQ(e, tr::kEarliestBuildDate);
  REQUIRE(tr::parse_build_date("Feb 29 2028", e));
  CHECK_EQ(e, 1835395200);
  CHECK(!tr::parse_build_date(nullptr, e));
  CHECK(!tr::parse_build_date("", e));
  CHECK(!tr::parse_build_date("Sept 2 2026", e));
  CHECK(!tr::parse_build_date("Foo 22 2026", e));
  CHECK(!tr::parse_build_date("Sep 32 2026", e));
  CHECK(!tr::parse_build_date("Sep 22 20x6", e));
  CHECK(!tr::parse_build_date("Sep 22 2026 ", e));
}

TEST_CASE("time: an NTP answer before the build date is refused; the file floor allows the longest retention") {
  int64_t build = 0;
  REQUIRE(tr::parse_build_date("Sep 22 2026", build));
  const int64_t ntp = tr::ntp_floor(build);
  CHECK_EQ(ntp, build - tr::kDay);  // the build machine's local date may be a day ahead of UTC
  CHECK(tr::accept_ntp(build, ntp));
  CHECK(tr::accept_ntp(build - 3600, ntp));  // built just after midnight in UTC+14
  CHECK(tr::accept_ntp(2000000000, ntp));
  CHECK(!tr::accept_ntp(0, ntp));             // 1970: a broken or spoofed server
  CHECK(!tr::accept_ntp(946684800, ntp));     // 2000
  CHECK(!tr::accept_ntp(ntp - 1, ntp));
  CHECK_EQ(tr::file_floor(build), ntp - 366 * tr::kDay);
}

TEST_CASE("time: without a router server the slots are the setting and the two fallbacks") {
  const tr::Slots actual = slots({kEmpty, kEmpty, kEmpty, kEmpty});
  const tr::Plan p = tr::plan(actual, "pool.ntp.org", true);
  CHECK(p == tr::Plan{Want::Setting, Want::Fallback0, Want::Fallback1, Want::Empty});
  const std::vector<tr::Repair> r = tr::repairs(actual, "pool.ntp.org", true);
  REQUIRE_EQ(r.size(), 3u);
  CHECK_EQ(r[0].slot, 0u);
  CHECK(r[0].want == Want::Setting);
  CHECK(r[2].want == Want::Fallback1);
  // Once written (and resolved: a named slot carries its address too), nothing to do.
  const tr::Slots done = slots({named("pool.ntp.org", true), named("time.google.com"), named("time.cloudflare.com"), kEmpty});
  CHECK(tr::repairs(done, "pool.ntp.org", true).empty());
}

TEST_CASE("time: a router server keeps slot 0 and the rest move down after DHCP cleared them") {
  // What lwIP leaves after a DHCP ACK with option 42: slot 0 by address, 1..3 cleared.
  const tr::Slots after_ack = slots({kDhcp, kEmpty, kEmpty, kEmpty});
  CHECK(tr::from_dhcp(after_ack[0]));
  const std::vector<tr::Repair> r = tr::repairs(after_ack, "pool.ntp.org", true);
  REQUIRE_EQ(r.size(), 3u);
  CHECK_EQ(r[0].slot, 1u);
  CHECK(r[0].want == Want::Setting);
  CHECK_EQ(r[1].slot, 2u);
  CHECK(r[1].want == Want::Fallback0);
  CHECK_EQ(r[2].slot, 3u);
  CHECK(r[2].want == Want::Fallback1);
  const tr::Slots restored = slots({kDhcp, named("pool.ntp.org"), named("time.google.com"), named("time.cloudflare.com")});
  CHECK(tr::repairs(restored, "pool.ntp.org", true).empty());
  // After a disconnect the router's server is forgotten: the setting takes slot 0 back.
  const std::vector<tr::Repair> gone = tr::repairs(restored, "pool.ntp.org", false);
  REQUIRE_EQ(gone.size(), 4u);
  CHECK(gone[0].want == Want::Setting);
  CHECK(gone[3].want == Want::Empty);
}

TEST_CASE("time: a setting equal to a fallback is not asked twice") {
  const tr::Slots actual = slots({kEmpty, kEmpty, kEmpty, kEmpty});
  CHECK(tr::plan(actual, "TIME.google.com", true) == tr::Plan{Want::Setting, Want::Fallback1, Want::Empty, Want::Empty});
  CHECK(tr::plan(slots({kDhcp, kEmpty, kEmpty, kEmpty}), "time.cloudflare.com", true) ==
        tr::Plan{Want::Keep, Want::Setting, Want::Fallback0, Want::Empty});
}

TEST_CASE("time: a changed setting rewrites only its own slot") {
  const tr::Slots actual = slots({kDhcp, named("pool.ntp.org"), named("time.google.com"), named("time.cloudflare.com")});
  const std::vector<tr::Repair> r = tr::repairs(actual, "ntp.example.org", true);
  REQUIRE_EQ(r.size(), 1u);
  CHECK_EQ(r[0].slot, 1u);
  CHECK(r[0].want == Want::Setting);
}

TEST_CASE("time: the wait for the first answer is reported at 60 s, then every 30 minutes") {
  CHECK(!tr::wait_warning_due(59, -1));
  CHECK(tr::wait_warning_due(60, -1));
  CHECK(!tr::wait_warning_due(61, 60));
  CHECK(!tr::wait_warning_due(60 + 1799, 60));
  CHECK(tr::wait_warning_due(60 + 1800, 60));
}

}  // namespace

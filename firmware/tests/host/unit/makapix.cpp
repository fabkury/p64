// Host unit tests: the Makapix worker's policy (p64_makapix/src/policy.cpp) and the
// server's document shapes (p64_makapix/src/contract.cpp),
// against the examples in reference/makapix/docs/player/querying-artwork.md with a real
// UUID for the storage key.
#include "common.hpp"
#include "contract.hpp"
#include "policy.hpp"

namespace {

using p64::content::MakapixEntries;
using p64::content::MakapixEntry;
namespace contract = p64::makapix::contract;

constexpr const char *kUuid = "3f2a9c1e-5b7d-4e8f-9a0b-1c2d3e4f5a6b";

MakapixEntry parse(const std::string &json, bool *ok = nullptr) {
  cJSON *doc = cJSON_Parse(json.c_str());
  REQUIRE(doc);
  MakapixEntry e;
  const bool r = contract::entry_from_post(doc, e);
  if (ok) *ok = r;
  cJSON_Delete(doc);
  return e;
}

TEST_CASE("makapix contract: a query_posts post becomes an index entry") {
  bool ok = false;
  const MakapixEntry e = parse(std::string(R"({"post_id":12345,"kind":"artwork","created_at":"2024-01-15T09:00:00Z",
    "storage_key":")") + kUuid + R"(","art_url":"https://vault.makapix.club/21/32/)" + kUuid + R"(.png",
    "storage_shard":"21/32","native_format":"png","width":64,"height":48,"frame_count":1,"public_sqid":"BVLa",
    "artwork_modified_at":"2024-02-01T00:00:00Z"})", &ok);
  REQUIRE(ok);
  CHECK_EQ(e.post_id, 12345);
  CHECK(std::string(e.shard) == "21/32");
  CHECK(e.format == static_cast<uint8_t>(p64::content::MakapixFormat::Png));
  CHECK_EQ(e.width, 64);
  CHECK_EQ(e.height, 48);
  CHECK(std::string(e.sqid) == "BVLa");
  CHECK_EQ(e.created_at, 1705309200u);  // 2024-01-15T09:00:00Z
  CHECK(e.modified_at > e.created_at);
  CHECK(p64::content::format_uuid(e.storage_key) == kUuid);
  CHECK(contract::download_url(e, "vault.makapix.club", false) ==
        std::string("http://vault.makapix.club/21/32/") + kUuid + ".png");
  CHECK(contract::download_url(e, "vault.makapix.club", true).rfind("https://", 0) == 0);
}

TEST_CASE("makapix contract: the promoted feed's shape (id, files, storage_shard)") {
  bool ok = false;
  const MakapixEntry e = parse(std::string(R"({"id":7,"storage_key":")") + kUuid +
                               R"(","storage_shard":"a1/b2","files":[{"format":"png","is_native":false},
    {"format":"gif","is_native":true}],"width":32,"height":32})", &ok);
  REQUIRE(ok);
  CHECK_EQ(e.post_id, 7);
  CHECK(std::string(e.shard) == "a1/b2");
  CHECK(e.format == static_cast<uint8_t>(p64::content::MakapixFormat::Gif));
}

TEST_CASE("makapix contract: what is not an artwork entry is refused") {
  bool ok = true;
  const std::string key = std::string(R"("storage_key":")") + kUuid + R"(")";
  parse(R"({"post_id":1,"kind":"playlist",)" + key + R"(,"native_format":"png"})", &ok);
  CHECK(!ok);
  parse(R"({"kind":"artwork",)" + key + R"(,"native_format":"png"})", &ok);  // no id
  CHECK(!ok);
  parse(R"({"post_id":1,"storage_key":"abc123-def456","native_format":"png"})", &ok);  // not a UUID
  CHECK(!ok);
  parse(R"({"post_id":1,)" + key + R"(,"native_format":"jpeg"})", &ok);  // no decoder for it
  CHECK(!ok);
}

TEST_CASE("makapix contract: a page appends artworks and reads the cursor") {
  const std::string post = std::string(R"({"post_id":1,"storage_key":")") + kUuid + R"(","native_format":"webp"})";
  const std::string junk = R"({"post_id":2,"kind":"playlist"})";
  cJSON *doc = cJSON_Parse((R"({"posts":[)" + post + "," + junk + "," + post + R"(],"next_cursor":"50","has_more":true})").c_str());
  REQUIRE(doc);
  MakapixEntries out;
  std::string cursor;
  bool more = false;
  contract::fill_page(doc, "posts", out, cursor, more);
  cJSON_Delete(doc);
  CHECK_EQ(out.size(), 2u);
  CHECK(cursor == "50");
  CHECK(more);
  doc = cJSON_Parse(R"({"items":[],"next_cursor":null,"has_more":true})");
  contract::fill_page(doc, "items", out, cursor, more);
  cJSON_Delete(doc);
  CHECK(cursor.empty());
  CHECK(!more);  // no cursor, no next page, whatever has_more says
  doc = cJSON_Parse(R"({"items":[],"next_cursor":"x"})");
  contract::fill_page(doc, "items", out, cursor, more);
  cJSON_Delete(doc);
  CHECK(more);  // a cursor without has_more means more
}

// --- the worker's policy (policy.hpp) ----------------------------------------------

namespace policy = p64::makapix::policy;

struct FakeChannel {
  bool active = true, refreshing = false, loaded = true;
  int64_t retry_at_us = 0, next_refresh_us = 0;
  MakapixEntries entries;
  uint32_t download_cursor = 0;
};

MakapixEntry entry(int32_t id, uint8_t flags = 0, uint16_t side = 64) {
  MakapixEntry e{};
  e.post_id = id;
  e.flags = flags;
  e.width = e.height = side;
  return e;
}

TEST_CASE("makapix policy: a walk in progress first, then an unloaded index, then a due refresh") {
  std::vector<std::unique_ptr<FakeChannel>> chs;
  for (int i = 0; i < 3; ++i) chs.push_back(std::make_unique<FakeChannel>());
  chs[0]->next_refresh_us = 5000;  // not due yet
  chs[1]->next_refresh_us = 5000;
  chs[2]->next_refresh_us = 5000;
  CHECK(policy::next_channel_needing_service(chs, 1000) == nullptr);
  chs[2]->loaded = false;
  CHECK(policy::next_channel_needing_service(chs, 1000) == chs[2].get());
  chs[1]->refreshing = true;
  CHECK(policy::next_channel_needing_service(chs, 1000) == chs[1].get());
  chs[1]->active = false;  // its channel left the playset: the walk pauses
  CHECK(policy::next_channel_needing_service(chs, 1000) == chs[2].get());
  chs[2]->loaded = true;
  chs[0]->next_refresh_us = 0;
  chs[0]->retry_at_us = 9000;  // failed recently: waits out the retry
  CHECK(policy::next_channel_needing_service(chs, 1000) == nullptr);
  CHECK(policy::next_channel_needing_service(chs, 9000) == chs[0].get());
}

TEST_CASE("makapix policy: an index from the card refreshes when older than the interval") {
  CHECK_EQ(policy::next_refresh_after_load(14400, 14400, 7), 0);
  CHECK_EQ(policy::next_refresh_after_load(100, 14400, 7), 7 + int64_t(14300) * policy::kSecond);
}

TEST_CASE("makapix policy: a walk resumed after a minute starts over (the dead connection, 2026-09-21)") {
  const int64_t s = policy::kSecond;
  CHECK(policy::begin_step(false, 0, 10 * s).first);             // no walk yet
  CHECK(!policy::begin_step(true, 10 * s, 12 * s).first);        // the next page, 2 s later
  const policy::StepStart late = policy::begin_step(true, 10 * s, 10 * s + 61 * s);
  CHECK((late.first && late.stale));                             // paused 61 s: over again
  CHECK(!policy::begin_step(true, 10 * s, 70 * s).stale);        // exactly 60 s is still fine
}

TEST_CASE("makapix policy: the first pages of an empty channel install at once (M6)") {
  // Scenario: a channel with no index walks 41 pages of 50; before the fix nothing played
  // until the last page. Now the first page goes in as soon as it lands.
  policy::PageOutcome o = policy::after_page(50, 2048, true, false, /*index_empty=*/true);
  CHECK((!o.done && o.install_now));
  o = policy::after_page(100, 2048, true, false, /*index_empty=*/false);  // later pages merge at the end
  CHECK((!o.done && !o.install_now));
  CHECK(policy::after_page(2048, 2048, true, false, false).done);  // the cache size caps the walk
  CHECK(policy::after_page(120, 2048, false, false, false).done);  // the server has no more
  CHECK(policy::after_page(120, 2048, true, true, false).done);    // an empty page ends it too
  CHECK(!policy::after_page(50, 2048, false, false, true).install_now);  // done: finish_walk installs
}

TEST_CASE("makapix policy: oversized entries are dropped from a page") {
  MakapixEntries page{entry(1, 0, 64), entry(2, 0, 256), entry(3, 0, 128)};
  CHECK_EQ(policy::drop_oversized(page, 128), 1u);
  REQUIRE_EQ(page.size(), 2u);
  CHECK_EQ(page[1].post_id, 3);
}

TEST_CASE("makapix policy: a failed refresh backs off from 30 s to 15 min") {
  const int64_t s = policy::kSecond;
  CHECK_EQ(policy::retry_delay_us(1, "HTTP 500"), 30 * s);
  CHECK_EQ(policy::retry_delay_us(2, "HTTP 500"), 60 * s);
  CHECK_EQ(policy::retry_delay_us(5, "HTTP 500"), 480 * s);
  CHECK_EQ(policy::retry_delay_us(6, "HTTP 500"), 900 * s);
  CHECK_EQ(policy::retry_delay_us(40, "HTTP 500"), 900 * s);
  CHECK_EQ(policy::retry_delay_us(1, "needs pairing"), 900 * s);
}

TEST_CASE("makapix policy: downloads go round-robin, each channel from its cursor, skipping what is done") {
  std::vector<std::unique_ptr<FakeChannel>> chs;
  chs.push_back(std::make_unique<FakeChannel>());
  chs.push_back(std::make_unique<FakeChannel>());
  chs[0]->entries = {entry(10, p64::content::kMakapixCached), entry(11), entry(12, p64::content::kMakapixMissing), entry(13)};
  chs[1]->entries = {entry(20), entry(21, p64::content::kMakapixRejected), entry(22)};
  size_t rr = 0, index = 0;
  std::vector<int32_t> order;
  // The worker flags each download cached; simulate that and walk until nothing is left.
  while (FakeChannel *ch = policy::next_download(chs, rr, index)) {
    order.push_back(ch->entries[index].post_id);
    ch->entries[index].flags |= p64::content::kMakapixCached;
    REQUIRE(order.size() < 20);
  }
  CHECK(order == std::vector<int32_t>{11, 20, 13, 22});
  chs[1]->active = false;
  chs[0]->entries.push_back(entry(14));
  CHECK(policy::next_download(chs, rr, index) == chs[0].get());
}

TEST_CASE("makapix policy: a Followed request made offline is parked, not failed (2026-09-21)") {
  using policy::JobDisposition;
  // At boot the show restores the saved Followed playset at 1.7 s; Wi-Fi joins at 4-6 s.
  CHECK(policy::job_disposition(false, false, /*followed=*/true, /*someone waits=*/false) == JobDisposition::Park);
  // Anything else offline fails at once, and so does a Followed that someone waits on.
  CHECK(policy::job_disposition(false, false, false, false) == JobDisposition::Fail);
  CHECK(policy::job_disposition(false, false, true, true) == JobDisposition::Fail);
  // Online, everything runs; likes run offline too (they report their own failure).
  CHECK(policy::job_disposition(true, false, true, false) == JobDisposition::Run);
  CHECK(policy::job_disposition(false, true, false, true) == JobDisposition::Run);
}

}  // namespace

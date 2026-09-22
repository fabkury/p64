// Host unit tests: the Makapix server's document shapes (p64_makapix/src/contract.cpp),
// against the examples in reference/makapix/docs/player/querying-artwork.md with a real
// UUID for the storage key.
#include "common.hpp"
#include "contract.hpp"

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

}  // namespace

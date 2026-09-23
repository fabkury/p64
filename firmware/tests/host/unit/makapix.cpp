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

// --- commands from the site and the payloads the device publishes (MQTT) ---------------

using Cmd = contract::Command;

Cmd command(const std::string &json) { return contract::parse_command(json.data(), json.size()); }

TEST_CASE("makapix commands: the documented examples (docs/mqtt-api/commands.md)") {
  Cmd c = command(R"({"command_id":"cmd-001","command_type":"swap_next","payload":{},"timestamp":"2024-01-15T10:30:00Z"})");
  CHECK(c.kind == Cmd::Kind::Next);
  CHECK(c.id == "cmd-001");
  CHECK(c.ack_status.empty());  // navigation is not acknowledged
  CHECK(command(R"({"command_id":"cmd-002","command_type":"swap_back","payload":{}})").kind == Cmd::Kind::Back);

  c = command(std::string(R"({"command_id":"cmd-003","command_type":"show_artwork","payload":{"post_id":12345,
    "storage_key":")") + kUuid + R"(","storage_shard":"21/32","native_format":"png","width":64,"height":64}})");
  REQUIRE(c.kind == Cmd::Kind::ShowArtwork);
  CHECK_EQ(c.entry.post_id, 12345);
  CHECK(std::string(c.entry.shard) == "21/32");
  CHECK(c.name == "post 12345");
  CHECK(contract::download_url(c.entry, "vault.makapix.club", false).find("/21/32/") != std::string::npos);

  c = command(R"({"command_id":"cmd-004","command_type":"play_channel","payload":{"channel_name":"promoted"}})");
  REQUIRE(c.kind == Cmd::Kind::PlayPlayset);
  REQUIRE_EQ(c.playset.channels.size(), 1u);
  CHECK(c.playset.channels[0].kind == p64::content::ChannelKind::MakapixPromoted);
  CHECK(c.playset.name == "Makapix");

  c = command(R"({"command_id":"cmd-005","command_type":"play_channel","payload":{"channel_name":"by_user",
    "user_sqid":"k5fNx","user_handle":"pixelartist"}})");
  REQUIRE(c.kind == Cmd::Kind::PlayPlayset);
  CHECK(c.playset.channels[0].kind == p64::content::ChannelKind::MakapixArtist);
  CHECK(c.playset.channels[0].identifier == "k5fNx");
  CHECK(c.playset.channels[0].display_name == "@pixelartist");

  c = command(R"({"command_id":"cmd-006","command_type":"play_channel","payload":{"channel_name":"hashtag","hashtag":"landscape"}})");
  REQUIRE(c.kind == Cmd::Kind::PlayPlayset);
  CHECK(c.playset.channels[0].identifier == "landscape");

  c = command(R"({"command_id":"cmd-007","command_type":"play_playset","payload":{"playset_name":"followed_artists",
    "channels":[{"type":"user","identifier":"k5fNx","display_name":"@pixelartist","weight":10},
    {"type":"named","name":"promoted","display_name":"Promoted","weight":5}],"exposure_mode":"manual","pick_mode":"recency"}})");
  REQUIRE(c.kind == Cmd::Kind::PlayPlayset);
  CHECK(c.playset.name == "Followed");  // the site's followed_artists is the Followed built-in
  CHECK(c.playset.builtin);
  CHECK_EQ(c.playset.channels.size(), 2u);
}

TEST_CASE("makapix commands: the set_* commands are acknowledged, ok or with the reason") {
  Cmd c = command(R"({"command_id":"a","command_type":"set_brightness","payload":{"value":40}})");
  CHECK(c.kind == Cmd::Kind::SetBrightness);
  CHECK_EQ(c.brightness, 40);
  CHECK((c.ack_status == "ok" && c.republish_state));
  c = command(R"({"command_id":"b","command_type":"set_brightness","payload":{"value":0}})");
  CHECK(c.kind == Cmd::Kind::None);
  CHECK((c.ack_status == "error" && c.ack_error == "brightness must be 1 to 255"));
  c = command(R"({"command_id":"c","command_type":"set_paused","payload":{"paused":true}})");
  CHECK((c.kind == Cmd::Kind::SetPaused && c.paused && c.ack_status == "ok"));
  c = command(R"({"command_id":"d","command_type":"set_paused","payload":{"paused":"yes"}})");
  CHECK((c.kind == Cmd::Kind::None && c.ack_status == "error"));
  c = command(R"({"command_id":"e","command_type":"set_rotation","payload":{"value":270}})");
  CHECK((c.kind == Cmd::Kind::SetRotation && c.rotation == 270));
  c = command(R"({"command_id":"f","command_type":"set_rotation","payload":{"value":45}})");
  CHECK(c.ack_error == "rotation must be 0, 90, 180, or 270");
  CHECK(command(R"({"command_id":"g","command_type":"set_mirror","payload":{}})").ack_status == "unsupported");
  CHECK(command(R"({"command_id":"h","command_type":"teleport","payload":{}})").ack_status == "unsupported");
  CHECK(command(R"({"command_type":"teleport","payload":{}})").ack_status.empty());  // no id, nobody to answer
}

TEST_CASE("makapix commands: unusable payloads are refused without acting") {
  CHECK(command("{not json").kind == Cmd::Kind::Invalid);
  CHECK(command(R"({"command_id":"x","command_type":"show_artwork","payload":{"post_id":1,"storage_key":"abc123-def456-789",
    "native_format":"png"}})").kind == Cmd::Kind::None);  // the docs' example key is not a UUID
  CHECK(command(R"({"command_id":"x","command_type":"play_channel","payload":{"channel_name":"giphy"}})").kind == Cmd::Kind::None);
  Cmd c = command(R"({"command_id":"x","command_type":"play_playset","payload":{"playset_name":"p",
    "channels":[{"type":"url_list","identifier":"x"}]}})");
  CHECK(c.kind == Cmd::Kind::None);  // URL lists are not supported yet: nothing to play
  CHECK(!c.warning.empty());
  // A p3a's "sdcard" channel is this device's own card.
  c = command(R"({"command_id":"x","command_type":"play_playset","payload":{"playset_name":"p",
    "channels":[{"type":"sdcard","identifier":""},{"type":"url_list","identifier":"x"}]}})");
  REQUIRE(c.kind == Cmd::Kind::PlayPlayset);
  REQUIRE_EQ(c.playset.channels.size(), 1u);
  CHECK(c.playset.channels[0].kind == p64::content::ChannelKind::Local);
}

TEST_CASE("makapix payloads: status, state, capabilities, view and ack") {
  CHECK(contract::status_json("7e98", 42, "0.1.0") ==
        R"({"player_key":"7e98","status":"online","current_post_id":42,"firmware_version":"0.1.0"})");
  CHECK(contract::status_json("7e98", -1, "0.1.0").find("current_post_id") == std::string::npos);
  CHECK(contract::state_json(true, 40, 90) == R"({"is_paused":true,"brightness":40,"rotation":90})");
  CHECK(contract::capabilities_json("0.1.0") ==
        R"({"firmware_version":"0.1.0","features":{"pause":{},"brightness":{"min":1,"max":255,"step":1},)"
        R"("rotation":{"values":[0,90,180,270]}}})");
  contract::ViewEvent v;
  v.post_id = 7;
  v.timestamp = "2026-09-22T12:00:00Z";
  v.channel = "by_user";
  v.user_sqid = "k5fNx";
  const std::string view = contract::view_json(v, "7e98");
  CHECK(view.find(R"("intent":"channel")") != std::string::npos);
  CHECK(view.find(R"("channel_user_sqid":"k5fNx")") != std::string::npos);
  CHECK(view.find("channel_hashtag") == std::string::npos);
  CHECK(contract::ack_json("cmd-1", "ok", nullptr) == R"({"command_id":"cmd-1","status":"ok","error":null})");
  CHECK(contract::ack_json("cmd-1", "error", "no") == R"({"command_id":"cmd-1","status":"error","error":"no"})");
}

TEST_CASE("makapix policy: the nightly sweep deletes what was not played, and what an untrusted clock wrote") {
  using V = policy::SweepVerdict;
  const int64_t now = 1790000000;  // 2026-09-21
  const int64_t day = 86400;
  const uint32_t thirty = 30 * 86400;
  // The file floor of a firmware built 2026-09-22 (ADR 0011): build date - 1 day - 366 days.
  int64_t build = 0;
  REQUIRE(p64::net::time_rules::parse_build_date("Sep 22 2026", build));
  const int64_t floor = p64::net::time_rules::file_floor(build);
  CHECK(policy::sweep_verdict(now - thirty, now, thirty, floor) == V::Keep);        // exactly the retention: kept
  CHECK(policy::sweep_verdict(now - thirty - 1, now, thirty, floor) == V::Delete);  // one second older: goes
  CHECK(policy::sweep_verdict(now - 60, now, thirty, floor) == V::Keep);            // played a minute ago
  // FAT stamped 1980 while the clock read 1970 (a cold boot before NTP): below the floor.
  CHECK(policy::sweep_verdict(315532800, now, 365u * 86400u, floor) == V::Delete);
  // Played ten days before the build of the firmware now running: inside a 30-day
  // retention, so kept (a build-date floor would have deleted it after every update).
  CHECK(policy::sweep_verdict(build - 10 * day, now, thirty, floor) == V::Keep);
  // A few seconds or hours ahead is recent (a clock stepped back, FAT's two-second
  // rounding right after a touch). Before 2026-09-22 the unsigned difference wrapped and
  // these were deleted as ancient.
  CHECK(policy::sweep_verdict(now + 2, now, thirty, floor) == V::Keep);
  CHECK(policy::sweep_verdict(now + 3600, now, thirty, floor) == V::Keep);
  CHECK(policy::sweep_verdict(now + day, now, thirty, floor) == V::Keep);
  // More than a day ahead: the card or the clock is suspect; the sweep deletes nothing
  // (before ADR 0011 such a file was deleted, and a clock set a year back by hand wiped
  // the whole cache).
  CHECK(policy::sweep_verdict(now + day + 1, now, thirty, floor) == V::Suspect);
  CHECK(policy::sweep_verdict(now + 400 * day, now, thirty, floor) == V::Suspect);
}

}  // namespace

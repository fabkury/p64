// Host unit tests: playsets, their JSON, the scheduler, history and the Makapix index.
#include "common.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


// --- p64_content (M5): playset model, JSON, scheduler, history ---------------------

TEST_CASE("playset_model") {
  using namespace p64::content;
  CHECK(valid_playset_name("mix_1"));
  CHECK(!valid_playset_name(""));
  CHECK(!valid_playset_name("has space"));
  CHECK(!valid_playset_name(std::string(33, 'a')));
  Builtin b;
  CHECK((builtin_from_name("local", b) && b == Builtin::Local));
  CHECK((builtin_from_name("PROMOTED", b) && b == Builtin::Promoted));
  CHECK(!builtin_from_name("mine", b));
  Playset p;
  p.name = "Local";
  p.channels.push_back(ChannelSpec{ChannelKind::Local, "", "", 0, 0});
  std::string e;
  CHECK(!p.validate(e));  // reserved name
  p.name = "mine";
  CHECK(p.validate(e));
  p.channels.push_back(ChannelSpec{ChannelKind::MakapixArtist, "bad sqid!", "", 1, 0});
  CHECK(!p.validate(e));
  p.channels.back().identifier = "Ab12";
  CHECK(p.validate(e));
  p.channels.push_back(ChannelSpec{ChannelKind::Pinned, "x", "", 0, 0});
  CHECK(!p.validate(e));
  p.channels.pop_back();
  CHECK(p.channels[0].default_display_name() == "animations");
  CHECK(p.channels[1].default_display_name() == "Artist Ab12");
  CHECK(!p.equal_weights());
  Playset loc = builtin_playset(Builtin::Local, {"", "pixel", "photos"});
  CHECK_EQ(loc.channels.size(), 3u);
  CHECK(loc.builtin);
  CHECK(loc.equal_weights());
  CHECK(loc.validate(e));
  CHECK(builtin_playset(Builtin::Followed, {}).channels.empty());
  CHECK(builtin_playset(Builtin::Promoted, {}).channels[0].kind == ChannelKind::MakapixPromoted);
}


TEST_CASE("playset_json") {
  using namespace p64::content;
  const char *text =
      "{\"name\":\"mix\",\"channels\":[{\"kind\":\"local\",\"identifier\":\"pixel\",\"weight\":2},"
      "{\"type\":\"named\",\"name\":\"promoted\",\"weight\":1},"
      "{\"type\":\"user\",\"identifier\":\"Q1\",\"display_name\":\"Q\",\"offset\":3}]}";
  cJSON *j = cJSON_Parse(text);
  CHECK(j != nullptr);
  Playset p;
  std::string e;
  CHECK(playset_from_json(j, p, e));
  cJSON_Delete(j);
  CHECK(p.name == "mix");
  CHECK_EQ(p.channels.size(), 3u);
  CHECK((p.channels[0].kind == ChannelKind::Local && p.channels[0].identifier == "pixel" && p.channels[0].weight == 2));
  CHECK(p.channels[1].kind == ChannelKind::MakapixPromoted);
  CHECK((p.channels[2].kind == ChannelKind::MakapixArtist && p.channels[2].offset == 3 && p.channels[2].display_name == "Q"));
  CHECK(p.validate(e));
  cJSON *out = playset_to_json(p);
  char *s = cJSON_PrintUnformatted(out);
  cJSON_Delete(out);
  std::string round(s);
  cJSON_free(s);
  CHECK(round.find("\"kind\":\"promoted\"") != std::string::npos);
  CHECK(round.find("\"display_name\":\"Promoted\"") != std::string::npos);
  cJSON *j2 = cJSON_Parse(round.c_str());
  Playset p2;
  CHECK(playset_from_json(j2, p2, e));
  cJSON_Delete(j2);
  CHECK_EQ(p2.channels.size(), 3u);
  CHECK_EQ(p2.channels[2].offset, 3u);
  cJSON *bad = cJSON_Parse("{\"channels\":[{\"kind\":\"giphy\"}]}");
  CHECK(!playset_from_json(bad, p, e));
  cJSON_Delete(bad);
  bad = cJSON_Parse("{\"channels\":5}");
  CHECK(!playset_from_json(bad, p, e));
  cJSON_Delete(bad);
}


TEST_CASE("scheduler_weights") {
  using namespace p64::content;
  Scheduler s;
  s.configure({0, 0, 0}, {0, 0, 0}, 42);
  CHECK_EQ(s.select_channel(), -1);  // nothing available
  s.set_count(0, 10);
  s.set_count(1, 10);
  s.set_count(2, 0);
  CHECK_EQ(s.channel(0).weight, 32768u);
  CHECK_EQ(s.channel(1).weight, 32768u);
  CHECK_EQ(s.channel(2).weight, 0u);
  s.configure({3, 1, 0}, {0, 0, 0}, 42);
  s.set_count(0, 5);
  s.set_count(1, 5);
  s.set_count(2, 5);
  CHECK_EQ(s.channel(0).weight, 49152u);
  CHECK_EQ(s.channel(1).weight, 16384u);
  CHECK_EQ(s.channel(2).weight, 0u);  // muted
  // SWRR: exactly 3:1 over every 4 picks, the muted channel never.
  s.set_channel_select(ChannelSelect::Swrr);
  int counts[3] = {0, 0, 0};
  for (int i = 0; i < 400; ++i) {
    const int c = s.select_channel();
    CHECK((c >= 0 && c < 3));
    if (c >= 0) ++counts[c];
  }
  CHECK_EQ(counts[0], 300);
  CHECK_EQ(counts[1], 100);
  CHECK_EQ(counts[2], 0);
  bool consecutive = false;
  int last = -1;
  for (int i = 0; i < 100; ++i) {
    const int c = s.select_channel();
    if (c == 1 && last == 1) consecutive = true;
    last = c;
  }
  CHECK(!consecutive);
  // Stochastic: the shares hold within 5 % over 4000 picks.
  s.configure({3, 1, 0}, {0, 0, 0}, 7);
  s.set_count(0, 5);
  s.set_count(1, 5);
  s.set_count(2, 5);
  s.set_channel_select(ChannelSelect::Stochastic);
  int sc[3] = {0, 0, 0};
  for (int i = 0; i < 4000; ++i) {
    const int c = s.select_channel();
    if (c >= 0) ++sc[c];
  }
  CHECK((sc[0] > 2850 && sc[0] < 3150));
  CHECK((sc[1] > 850 && sc[1] < 1150));
  CHECK_EQ(sc[2], 0);
  // Equal weights among the channels with entries; an emptied channel gets no picks.
  s.configure({0, 0}, {0, 0}, 1);
  s.set_count(0, 3);
  s.set_count(1, 3);
  s.set_channel_select(ChannelSelect::Swrr);
  int eq[2] = {0, 0};
  for (int i = 0; i < 10; ++i) ++eq[s.select_channel()];
  CHECK_EQ(eq[0], 5);
  CHECK_EQ(eq[1], 5);
  s.set_count(1, 0);
  for (int i = 0; i < 5; ++i) CHECK_EQ(s.select_channel(), 0);
  CHECK_EQ(s.available_channels(), 1u);
}


TEST_CASE("scheduler_picks") {
  using namespace p64::content;
  Scheduler s;
  s.configure({0}, {2}, 99);
  CHECK_EQ(s.pick_entry(0, -1), -1);
  s.set_count(0, 5);
  s.set_pick_mode(PickMode::Recency);
  CHECK_EQ(s.pick_entry(0, -1), 2);  // the cursor starts at the offset
  CHECK_EQ(s.pick_entry(0, -1), 3);
  CHECK_EQ(s.pick_entry(0, -1), 4);
  CHECK_EQ(s.pick_entry(0, -1), 0);  // and wraps
  CHECK_EQ(s.pick_entry(0, -1), 1);
  CHECK_EQ(s.pick_entry(0, -1), 2);
  CHECK_EQ(s.pick_entry(0, 3), 4);  // the current artwork is skipped
  s.reset_cursor(0);
  CHECK_EQ(s.pick_entry(0, -1), 2);
  s.configure({0}, {7}, 99);
  s.set_count(0, 5);
  s.set_pick_mode(PickMode::Recency);
  CHECK_EQ(s.pick_entry(0, -1), 2);  // an offset beyond the count wraps
  s.set_count(0, 2);
  const int shrunk = s.pick_entry(0, -1);
  CHECK((shrunk >= 0 && shrunk < 2));
  // Random: in range, no immediate repeat when avoidable, every entry visited.
  s.configure({0}, {0}, 5);
  s.set_count(0, 8);
  s.set_pick_mode(PickMode::Random);
  bool seen[8] = {false, false, false, false, false, false, false, false};
  int last = -1;
  bool repeat = false;
  for (int i = 0; i < 400; ++i) {
    const int p = s.pick_entry(0, last);
    CHECK((p >= 0 && p < 8));
    if (p == last) repeat = true;
    seen[p] = true;
    last = p;
  }
  CHECK(!repeat);
  for (bool b : seen) CHECK(b);
  s.set_count(0, 1);
  CHECK_EQ(s.pick_entry(0, 0), 0);  // a single entry repeats, there is nothing else
  Scheduler a, b;
  a.configure({1, 1}, {0, 0}, 1234);
  b.configure({1, 1}, {0, 0}, 1234);
  a.set_count(0, 9);
  a.set_count(1, 9);
  b.set_count(0, 9);
  b.set_count(1, 9);
  for (int i = 0; i < 50; ++i) {
    const int ca = a.select_channel(), cb = b.select_channel();
    CHECK_EQ(ca, cb);
    CHECK_EQ(a.pick_entry(ca, -1), b.pick_entry(cb, -1));
  }
}


TEST_CASE("history") {
  using namespace p64::content;
  History h;
  CHECK(h.current() == nullptr);
  CHECK(!h.back());
  CHECK(!h.forward());
  auto item = [](const std::string &name) {
    HistoryItem i;
    i.name = name;
    return i;
  };
  h.push(item("a"));
  h.push(item("b"));
  h.push(item("c"));
  CHECK_EQ(h.size(), 3u);
  CHECK_EQ(h.position(), 2u);
  CHECK(h.current()->name == "c");
  CHECK(h.back());
  CHECK(h.current()->name == "b");
  CHECK(h.back());
  CHECK(h.current()->name == "a");
  CHECK(!h.back());
  CHECK(h.forward());
  CHECK(h.current()->name == "b");
  h.push(item("d"));  // from the middle: what was ahead is discarded
  CHECK_EQ(h.size(), 3u);
  CHECK(h.current()->name == "d");
  CHECK(!h.forward());
  CHECK(h.back());
  CHECK(h.current()->name == "b");
  CHECK(h.go_to(2));
  CHECK(h.current()->name == "d");
  CHECK(!h.go_to(3));
  h.remove(0);
  CHECK_EQ(h.size(), 2u);
  CHECK(h.current()->name == "d");
  CHECK_EQ(h.position(), 1u);
  h.remove(1);
  CHECK(h.current()->name == "b");
  CHECK_EQ(h.position(), 0u);
  h.clear();
  for (int i = 0; i < 40; ++i) h.push(item(std::to_string(i)));
  CHECK_EQ(h.size(), 32u);
  CHECK(h.at(0).name == "8");
  CHECK(h.current()->name == "39");
  for (int i = 0; i < 31; ++i) CHECK(h.back());
  CHECK(!h.back());
  CHECK(h.current()->name == "8");
}


TEST_CASE("makapix_index") {
  using namespace p64::content;
  uint8_t key[16];
  CHECK(parse_uuid("550e8400-e29b-41d4-a716-446655440000", key));
  CHECK_EQ(key[0], 0x55);
  CHECK_EQ(key[15], 0x00);
  CHECK(format_uuid(key) == "550e8400-e29b-41d4-a716-446655440000");
  CHECK(parse_uuid("550E8400E29B41D4A716446655440000", key));
  CHECK(!parse_uuid("550e8400-e29b-41d4-a716-44665544000", key));
  CHECK(!parse_uuid("zz0e8400-e29b-41d4-a716-446655440000", key));
  std::string shard, file;
  MakapixFormat fmt;
  CHECK(split_art_url("https://vault.makapix.club/21/32/abc-def.png", shard, file, fmt));
  CHECK(shard == "21/32");
  CHECK(file == "abc-def.png");
  CHECK(fmt == MakapixFormat::Png);
  CHECK(split_art_url("http://vault.makapix.club/a1/b2/c3/k.webp?x=1", shard, file, fmt));
  CHECK(shard == "a1/b2/c3");
  CHECK(file == "k.webp");
  CHECK(fmt == MakapixFormat::WebP);
  CHECK(!split_art_url("nonsense", shard, file, fmt));
  CHECK_EQ(parse_iso8601_utc("1970-01-01T00:00:10Z"), 10u);
  CHECK_EQ(parse_iso8601_utc("2024-01-15T09:00:00Z"), 1705309200u);
  CHECK_EQ(parse_iso8601_utc("2024-01-15T09:00:00.123456+00:00"), 1705309200u);
  CHECK_EQ(parse_iso8601_utc("2024-01-15T10:00:00+01:00"), 1705309200u);
  CHECK_EQ(parse_iso8601_utc("garbage"), 0u);

  MakapixEntry a = {};
  a.post_id = 1;
  parse_uuid("550e8400-e29b-41d4-a716-446655440000", a.storage_key);
  std::snprintf(a.sqid, sizeof(a.sqid), "k5fNx");
  std::snprintf(a.shard, sizeof(a.shard), "21/32");
  a.format = static_cast<uint8_t>(MakapixFormat::Gif);
  a.flags = kMakapixCached;
  a.width = a.height = 64;
  a.modified_at = 100;
  MakapixEntry b = a;
  b.post_id = 2;
  b.flags = kMakapixMissing;
  b.storage_key[0] = 0xAB;
  CHECK(cache_relative_path(b) == "cache/ab/ab0e8400-e29b-41d4-a716-446655440000.gif");
  MakapixEntries prev;
  prev.push_back(a);
  prev.push_back(b);
  const std::vector<uint8_t> bytes = serialize_index(prev);
  CHECK_EQ(bytes.size(), 16u + 2 * 64u);
  MakapixEntries back;
  std::string e;
  CHECK(deserialize_index(bytes.data(), bytes.size(), back, e));
  CHECK_EQ(back.size(), 2u);
  CHECK_EQ(back[1].post_id, 2);
  CHECK(std::string(back[0].sqid) == "k5fNx");
  std::vector<uint8_t> corrupt = bytes;
  corrupt[20] ^= 1;
  CHECK(!deserialize_index(corrupt.data(), corrupt.size(), back, e));
  CHECK(!deserialize_index(bytes.data(), bytes.size() - 1, back, e));
  CHECK(!deserialize_index(bytes.data(), 3, back, e));
  MakapixEntries empty;
  const std::vector<uint8_t> none = serialize_index(empty);
  CHECK((deserialize_index(none.data(), none.size(), back, e) && back.empty()));

  // Merge: a fresh listing without post 1, with post 2 unchanged and a new post 3.
  MakapixEntries fresh;
  MakapixEntry b2 = b;
  b2.flags = 0;
  b2.sqid[0] = 0;  // the RPC listing carries no sqid: keep the old one
  fresh.push_back(b2);
  MakapixEntry c = a;
  c.post_id = 3;
  c.flags = 0;
  fresh.push_back(c);
  CHECK_EQ(merge_index(prev, fresh), 1u);  // post 1 dropped
  CHECK_EQ(fresh.size(), 2u);
  CHECK_EQ(fresh[0].flags, kMakapixMissing);
  CHECK(std::string(fresh[0].sqid) == "k5fNx");
  CHECK_EQ(fresh[1].flags, 0);
  // A changed file (modified_at) loses its flags.
  MakapixEntries fresh2;
  MakapixEntry a2 = a;
  a2.flags = 0;
  a2.modified_at = 200;
  fresh2.push_back(a2);
  CHECK_EQ(merge_index(prev, fresh2), 1u);
  CHECK_EQ(fresh2[0].flags, 0);
}

}  // namespace

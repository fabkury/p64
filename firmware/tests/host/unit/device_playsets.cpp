// Host unit tests: device playsets, the user playsets of a cardless p64 (ADR 0018;
// docs/testing-strategy/no-card-mode.md, NC-H4).
#include <map>

#include "common.hpp"
#include "p64/content/device_playsets.hpp"

namespace {

using namespace p64::content;
namespace dp = p64::content::device_playsets;

struct FakeNvs {
  std::map<std::string, std::string> kv;
  int writes = 0;
  bool full = false;  // every write fails
  dp::Backend backend() {
    dp::Backend b;
    b.get = [this](const std::string &k, std::string &v) {
      const auto it = kv.find(k);
      if (it == kv.end()) return false;
      v = it->second;
      return true;
    };
    b.set = [this](const std::string &k, const std::string &v) {
      if (full) return false;
      kv[k] = v;
      ++writes;
      return true;
    };
    b.erase = [this](const std::string &k) {
      kv.erase(k);
      return true;
    };
    return b;
  }
};

Playset mix(const std::string &name, size_t channels = 2, const std::string &display = "") {
  Playset p;
  p.name = name;
  for (size_t i = 0; i < channels; ++i) {
    ChannelSpec c{ChannelKind::MakapixHashtag, "tag" + std::to_string(i), display, 100, 0};
    p.channels.push_back(c);
  }
  return p;
}

TEST_CASE("device playsets: save, list, load, delete, and they are where a restart finds them") {
  FakeNvs nvs;
  const dp::Backend b = nvs.backend();
  std::string error;
  std::vector<store::Summary> all;
  CHECK(dp::list(b, all, error));
  CHECK(all.empty());
  Playset p = mix("Desk mix", 3);
  REQUIRE(dp::save(b, p, error));
  Playset q = mix("another", 1);
  REQUIRE(dp::save(b, q, error));
  CHECK(dp::list(b, all, error));
  REQUIRE(all.size() == 2);
  CHECK(all[0].name == "another");  // sorted by canonical name
  CHECK(all[1].name == "Desk mix");
  CHECK(all[1].channels == 3);
  CHECK(dp::exists(b, "desk  MIX"));  // the same playset up to case and spacing (ADR 0017)
  Playset out;
  REQUIRE(dp::load(b, "DESK MIX", out, error));
  CHECK(out.name == "Desk mix");  // the stored spelling
  CHECK(out.channels.size() == 3);
  CHECK(out.channels[2].identifier == "tag2");
  CHECK(!out.builtin);
  // "A restart": a new backend over the same store.
  const dp::Backend again = nvs.backend();
  CHECK(dp::exists(again, "Desk mix"));
  REQUIRE(dp::remove(b, "desk mix", error));
  CHECK(!dp::exists(b, "Desk mix"));
  CHECK(!dp::remove(b, "Desk mix", error));
  CHECK(error == "no such playset");
  CHECK(!dp::load(b, "Desk mix", out, error));
}

TEST_CASE("device playsets: an update keeps its slot and its spelling") {
  FakeNvs nvs;
  const dp::Backend b = nvs.backend();
  std::string error;
  Playset p = mix("Desk mix", 2);
  REQUIRE(dp::save(b, p, error));
  Playset again = mix("desk MIX", 5);
  REQUIRE(dp::save(b, again, error));
  CHECK(again.name == "Desk mix");  // written back: an update, not a second playset
  std::vector<store::Summary> all;
  dp::list(b, all, error);
  REQUIRE(all.size() == 1);
  CHECK(all[0].channels == 5);
  CHECK(nvs.kv.size() == 1);
}

TEST_CASE("device playsets: the limits, each refused with its own words") {
  FakeNvs nvs;
  const dp::Backend b = nvs.backend();
  std::string error;
  for (size_t i = 0; i < dp::kMaxPlaysets; ++i) {
    Playset p = mix("mix " + std::to_string(i), 1);
    REQUIRE(dp::save(b, p, error));
  }
  Playset ninth = mix("one more", 1);
  CHECK(!dp::save(b, ninth, error));
  CHECK(error.find("already 8 playsets") != std::string::npos);
  Playset update = mix("mix 3", 4);  // an existing one can still change
  CHECK(dp::save(b, update, error));
  REQUIRE(dp::remove(b, "mix 0", error));
  CHECK(dp::save(b, ninth, error));  // and a freed slot is used again

  FakeNvs other;
  const dp::Backend o = other.backend();
  Playset wide = mix("wide", 17);
  CHECK(!dp::save(o, wide, error));
  CHECK(error.find("at most 16 channels") != std::string::npos);
  Playset sixteen = mix("sixteen", 16);
  CHECK(dp::save(o, sixteen, error));
  Playset local = mix("with a folder", 1);
  local.channels.push_back(ChannelSpec{ChannelKind::Local, "cats", "", 100, 0});
  CHECK(!dp::save(o, local, error));
  CHECK(error == "local channels need a card");
  Playset builtin = mix("Promoted", 1);
  CHECK(!dp::save(o, builtin, error));  // a reserved name
  Playset unnamed = mix("", 1);
  CHECK(!dp::save(o, unnamed, error));
  CHECK(other.kv.size() == 1);
}

TEST_CASE("device playsets: the largest playset fits its slot, and a store that is full says so") {
  FakeNvs nvs;
  const dp::Backend b = nvs.backend();
  std::string error;
  // 16 channels with the longest names a channel may have still fit one slot (an NVS
  // string holds 4000 bytes): the slot limit is a guard, the total is what stops a user.
  Playset fat = mix("fat", 16, std::string(64, 'x'));
  for (ChannelSpec &c : fat.channels) c.identifier = std::string(58, 'y') + c.identifier;
  REQUIRE(dp::save(b, fat, error));
  CHECK(dp::bytes_used(b) > 2500);
  CHECK(dp::bytes_used(b) <= dp::kMaxPlaysetBytes);
  REQUIRE(dp::remove(b, "fat", error));
  // The total: playsets of about 3 KB each stop at 16 KB whatever the count.
  size_t saved = 0;
  for (size_t i = 0; i < dp::kMaxPlaysets; ++i) {
    Playset p = mix("big " + std::to_string(i), 16, std::string(64, 'x'));
    for (ChannelSpec &c : p.channels) c.identifier = std::string(40, 'y') + c.identifier;
    if (dp::save(b, p, error)) ++saved;
  }
  CHECK(saved >= 3);
  CHECK(saved < dp::kMaxPlaysets);
  CHECK(error.find("full") != std::string::npos);
  CHECK(dp::bytes_used(b) <= dp::kMaxTotalBytes);
  // The store itself refuses the write: said, and nothing half-written is listed.
  FakeNvs broken;
  broken.full = true;
  Playset p = mix("x", 1);
  CHECK(!dp::save(broken.backend(), p, error));
  CHECK(error == "the device could not store the playset");
}

TEST_CASE("device playsets: rename, in place") {
  FakeNvs nvs;
  const dp::Backend b = nvs.backend();
  std::string error;
  Playset a = mix("first", 2), c = mix("second", 1);
  REQUIRE(dp::save(b, a, error));
  REQUIRE(dp::save(b, c, error));
  CHECK(!dp::rename(b, "first", "SECOND", error));  // taken, in any spelling
  CHECK(error.find("already exists") != std::string::npos);
  CHECK(!dp::rename(b, "first", "Local", error));
  CHECK(!dp::rename(b, "nobody", "x", error));
  CHECK(error == "no such playset");
  REQUIRE(dp::rename(b, "first", "Café 東京", error));
  CHECK(!dp::exists(b, "first"));
  Playset out;
  REQUIRE(dp::load(b, "café 東京", out, error));
  CHECK(out.name == "Café 東京");
  CHECK(out.channels.size() == 2);
  REQUIRE(dp::rename(b, "Café 東京", "CAFé  東京", error));  // a respelling: ASCII case and spacing only (ADR 0017)
  REQUIRE(dp::load(b, "Café 東京", out, error));
  CHECK(out.name == "CAFé  東京");
  CHECK(nvs.kv.size() == 2);
}

TEST_CASE("device playsets: a slot that does not parse is skipped and written over") {
  FakeNvs nvs;
  const dp::Backend b = nvs.backend();
  std::string error;
  nvs.kv["dps0"] = "{not json";
  nvs.kv["dps1"] = "{\"name\":\"\\u0001bad\",\"channels\":[]}";
  Playset p = mix("good", 1);
  REQUIRE(dp::save(b, p, error));
  std::vector<store::Summary> all;
  CHECK(dp::list(b, all, error));
  REQUIRE(all.size() == 1);
  CHECK(all[0].name == "good");
  // With every other slot taken, the broken ones are reused instead of refusing.
  for (int i = 0; i < 7; ++i) {
    Playset q = mix("n" + std::to_string(i), 1);
    CHECK(dp::save(b, q, error));
  }
  dp::list(b, all, error);
  CHECK(all.size() == 8);
  CHECK(nvs.kv["dps0"].find("not json") == std::string::npos);
}

}  // namespace

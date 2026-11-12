// Host unit tests: the memory window (ADR 0018; docs/testing-strategy/no-card-mode.md,
// NC-H1 and the pace of NC-H2).
#include "common.hpp"
#include "p64/content/memory_window.hpp"

namespace {

using p64::content::FileBytes;
using p64::content::MemoryWindow;

std::vector<uint8_t> file(size_t n, uint8_t fill = 7) { return std::vector<uint8_t>(n, fill); }
constexpr int64_t kMinute = 60 * 1000000LL;

TEST_CASE("memory window: capacity in files, least recently played goes first") {
  MemoryWindow w(4, 1000);
  std::vector<std::string> gone;
  w.on_evicted([&](const std::string &k) { gone.push_back(k); });
  for (const char *k : {"a", "b", "c", "d"}) CHECK(w.put(k, file(10)));
  CHECK(w.stats().files == 4);
  CHECK(gone.empty());
  CHECK(w.take("a"));  // played: "a" is now the most recent, "b" the oldest
  CHECK(w.put("e", file(10)));
  CHECK(gone == std::vector<std::string>{"b"});
  CHECK(!w.has("b"));
  CHECK(w.has("a"));
  CHECK(w.stats().files == 4);
  CHECK(w.stats().evictions == 1);
}

TEST_CASE("memory window: capacity in bytes, and a file larger than the window") {
  MemoryWindow w(48, 100);
  std::vector<std::string> gone;
  w.on_evicted([&](const std::string &k) { gone.push_back(k); });
  CHECK(w.put("a", file(40)));
  CHECK(w.put("b", file(40)));
  CHECK(w.put("c", file(40)));  // 120 > 100: "a" goes
  CHECK(gone == std::vector<std::string>{"a"});
  CHECK(w.stats().bytes == 80);
  CHECK(!w.put("huge", file(101)));  // can never fit: refused, nothing evicted for it
  CHECK(gone.size() == 1);
  CHECK(w.stats().refused == 1);
  CHECK(w.stats().bytes == 80);
  CHECK(w.put("big", file(100)));  // fits alone: everything else goes
  CHECK(w.stats().files == 1);
  CHECK(w.stats().bytes == 100);
}

TEST_CASE("memory window: the files played most recently are never evicted") {
  MemoryWindow w(10, 1000000);
  for (int i = 0; i < 10; ++i) CHECK(w.put("k" + std::to_string(i), file(10)));
  for (int i = 0; i < 10; ++i) CHECK(w.take("k" + std::to_string(i)));  // all played; k2..k9 are the 8 protected
  CHECK(w.put("n1", file(10)));  // k0 goes
  CHECK(w.put("n2", file(10)));  // k1 goes
  CHECK(!w.has("k0"));
  CHECK(!w.has("k1"));
  CHECK(w.put("n3", file(10)));  // n1 goes (never played, the oldest unprotected); k2..k9 stay
  CHECK(!w.has("n1"));
  for (int i = 2; i < 10; ++i) CHECK(w.has("k" + std::to_string(i)));
  // Only protected files are left to evict: a file that needs their room is refused.
  MemoryWindow tight(8, 80);
  for (int i = 0; i < 8; ++i) {
    CHECK(tight.put("p" + std::to_string(i), file(10)));
    CHECK(tight.take("p" + std::to_string(i)));
  }
  CHECK(!tight.put("x", file(10)));
  CHECK(tight.stats().files == 8);
  CHECK(tight.stats().refused == 1);
}

TEST_CASE("memory window: bytes are shared, and outlive an eviction while held") {
  MemoryWindow w(2, 150);
  CHECK(w.put("a", file(100, 42)));
  {
    const FileBytes held = w.take("a");
    REQUIRE(held);
    CHECK(held.use_count() == 2);  // the window's and ours: one copy of the bytes
  }
  CHECK(!w.take("nothing"));
  CHECK(w.stats().misses == 1);
  CHECK(w.stats().hits == 1);
  // An evicted file whose bytes someone still plays stays valid.
  MemoryWindow w3(9, 1000000);
  CHECK(w3.put("old", file(100, 42)));
  const FileBytes playing = w3.take("old");
  for (int i = 0; i < 8; ++i) {
    CHECK(w3.put("n" + std::to_string(i), file(10)));
    CHECK(w3.take("n" + std::to_string(i)));
  }
  CHECK(w3.put("more", file(10)));  // "old" is the ninth most recently played: it goes
  CHECK(!w3.has("old"));
  CHECK(playing->size() == 100);
  CHECK((*playing)[99] == 42);
  CHECK(playing.use_count() == 1);
}

TEST_CASE("memory window: replacing a key tells nobody, removing and clearing") {
  MemoryWindow w(4, 1000);
  int told = 0;
  w.on_evicted([&](const std::string &) { ++told; });
  CHECK(w.put("a", file(10)));
  CHECK(w.put("a", file(20)));
  CHECK(w.stats().files == 1);
  CHECK(w.stats().bytes == 20);
  w.remove("a");
  CHECK(w.stats().files == 0);
  CHECK(w.put("b", file(10)));
  w.clear();
  CHECK(w.stats().bytes == 0);
  CHECK(told == 0);
}

TEST_CASE("memory window: the first fill is free, then one download per swap, one a minute") {
  MemoryWindow w(3, 1000000);
  int64_t now = 1000;
  for (const char *k : {"a", "b"}) {
    CHECK(w.claim_download());  // the first fill: every claim is granted
    CHECK(w.put(k, file(10)));
    w.note_swap(now);  // swaps during the fill grant nothing and cost nothing
  }
  CHECK(w.claim_download());
  CHECK(w.put("c", file(10)));  // full
  CHECK(w.stats().filled);
  CHECK(!w.admits_download(now));
  CHECK(!w.claim_download());  // no swap since: nothing new
  w.note_swap(now);
  CHECK(w.admits_download(now));
  CHECK(w.claim_download());
  CHECK(w.put("d", file(10)));  // the swap's one download
  CHECK(!w.claim_download());
  // Swaps every 5 s: the next download waits for the minute.
  for (int i = 1; i < 12; ++i) {
    w.note_swap(now + i * 5000000LL);
    CHECK(!w.claim_download());
  }
  w.note_swap(now + kMinute);
  // A claim whose download could not be kept is still that swap's download.
  CHECK(w.claim_download());
  CHECK(!w.put("huge", file(2000000)));
  CHECK(!w.claim_download());
  // An hour of swaps every 30 s: at most 60 downloads.
  MemoryWindow h(2, 1000000);
  CHECK(h.put("x", file(10)));
  CHECK(h.put("y", file(10)));
  int downloads = 0;
  for (int i = 0; i < 120; ++i) {
    const int64_t t = i * 30000000LL;
    h.note_swap(t);
    if (h.claim_download()) {
      CHECK(h.put("n" + std::to_string(i), file(10)));
      ++downloads;
    }
  }
  CHECK(downloads == 60);
  CHECK(h.stats().renewals == 60);
}

TEST_CASE("memory window: several providers share one renewal, and a claim with nothing to fetch is given back") {
  MemoryWindow w(2, 1000000);
  CHECK(w.put("a", file(10)));
  CHECK(w.put("b", file(10)));
  REQUIRE(w.stats().filled);
  w.note_swap(0);
  // Three workers ask in the same instant: one is granted (three downloads of one swap
  // were counted on the device before the claim existed, 2026-11-12).
  int granted = 0;
  for (int worker = 0; worker < 3; ++worker) granted += w.claim_download() ? 1 : 0;
  CHECK(granted == 1);
  // The one that claimed has nothing to download: it gives the claim back, another takes it.
  w.unclaim_download();
  CHECK(w.claim_download());
  CHECK(!w.claim_download());
  w.unclaim_download();
  w.unclaim_download();  // twice is once
  CHECK(w.claim_download());
  CHECK(!w.claim_download());
  // A play-this file is put without a claim and costs the channels nothing.
  w.note_swap(kMinute);
  CHECK(w.put("dl-x", file(10)));
  CHECK(w.claim_download());
  // During the first fill nothing is counted: giving back changes nothing.
  MemoryWindow f(10, 1000000);
  CHECK(f.claim_download());
  f.unclaim_download();
  CHECK(f.claim_download());
  CHECK(f.claim_download());
}

TEST_CASE("memory window: a window that never fills keeps admitting") {
  MemoryWindow w(48, 6 * 1024 * 1024);
  for (int i = 0; i < 10; ++i) CHECK(w.put("k" + std::to_string(i), file(1000)));
  CHECK(!w.stats().filled);
  CHECK(w.claim_download());
  // Bytes can fill it before files do.
  MemoryWindow b(48, 8000);
  for (int i = 0; i < 8; ++i) CHECK(b.put("k" + std::to_string(i), file(1000)));
  CHECK(b.stats().filled);
  CHECK(!b.admits_download(0));
}

}  // namespace

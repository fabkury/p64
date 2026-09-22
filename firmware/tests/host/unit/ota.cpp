// Host unit tests: the firmware version rule and the release document (release.cpp).
#include "common.hpp"
#include "release.hpp"

namespace {

using p64::gfx::Frame;
using p64::gfx::Rgb;


// --- firmware versions (spec 15.2) ---------------------------------------------------

TEST_CASE("versions") {
  using namespace p64::ota::version;
  Parsed p = parse("v1.2.3");
  CHECK(p.valid); CHECK_EQ(p.major, 1); CHECK_EQ(p.minor, 2); CHECK_EQ(p.patch, 3); CHECK(p.suffix.empty());
  p = parse("0.1.0-dev");
  CHECK(p.valid); CHECK(p.suffix == "dev");
  CHECK(!parse("1.2").valid);
  CHECK(!parse("abc").valid);
  CHECK(!parse("1.2.3x").valid);
  CHECK(is_newer("0.1.0", "0.1.0-dev"));      // a release beats the dev build of it
  CHECK(!is_newer("0.1.0-dev", "0.1.0"));
  CHECK(is_newer("0.2.0", "0.1.9"));
  CHECK(is_newer("1.0.0", "0.99.99"));
  CHECK(!is_newer("0.1.0", "0.1.0"));
  CHECK(is_newer("v0.1.1", "0.1.0-dev"));
  CHECK(!is_newer("0.0.9", "0.1.0-dev"));     // older numbers stay older despite the suffix
  CHECK_EQ(compare("1.0.0", "1.0.0-rc1"), 1);
  CHECK_EQ(compare("1.0.0-rc1", "1.0.0-rc2"), -1);
}

// --- the GitHub release document (release.cpp) -------------------------------------

namespace release = p64::ota::release;

// The shape of GET /repos/<repo>/releases/latest, trimmed to what the device reads.
const char *kRelease = R"({
  "tag_name": "v0.2.0",
  "name": "p64 0.2.0",
  "body": "Faster boot.\r\nThe show core is host-tested.",
  "assets": [
    {"name": "p64.bin.sha256", "size": 74,
     "browser_download_url": "https://github.com/fabkury/p64/releases/download/v0.2.0/p64.bin.sha256"},
    {"name": "p64.bin", "size": 2078240,
     "browser_download_url": "https://github.com/fabkury/p64/releases/download/v0.2.0/p64.bin"},
    {"name": "bootloader.bin", "size": 21392, "browser_download_url": "https://example.invalid/bootloader.bin"}
  ]
})";

TEST_CASE("ota release: the tag, the notes and the two assets") {
  release::Release r;
  std::string error;
  REQUIRE(release::parse(kRelease, std::strlen(kRelease), "p64.bin", 600, r, error));
  CHECK(r.version == "0.2.0");  // the leading 'v' goes
  CHECK(r.notes.rfind("Faster boot.", 0) == 0);
  CHECK(r.bin_url == "https://github.com/fabkury/p64/releases/download/v0.2.0/p64.bin");
  CHECK(r.sha_url == "https://github.com/fabkury/p64/releases/download/v0.2.0/p64.bin.sha256");
  CHECK_EQ(r.size, 2078240u);
  CHECK(p64::ota::version::is_newer(r.version, "0.1.0-dev"));
}

TEST_CASE("ota release: long notes are cut, a missing asset leaves its URL empty") {
  release::Release r;
  std::string error;
  REQUIRE(release::parse(kRelease, std::strlen(kRelease), "p64.bin", 10, r, error));
  CHECK(r.notes == "Faster boo...");
  REQUIRE(release::parse(kRelease, std::strlen(kRelease), "other.bin", 600, r, error));
  CHECK(r.bin_url.empty());
  CHECK(r.sha_url.empty());
}

TEST_CASE("ota release: unreadable JSON and a release without a tag are refused") {
  release::Release r;
  std::string error;
  CHECK(!release::parse("{nope", 5, "p64.bin", 600, r, error));
  CHECK(error == "release JSON unreadable");
  const char *untagged = R"({"assets":[]})";
  CHECK(!release::parse(untagged, std::strlen(untagged), "p64.bin", 600, r, error));
  CHECK(error == "release without a tag");
}

TEST_CASE("ota release: the checksum file as sha256sum writes it") {
  const std::string hex = "3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b";
  uint8_t d[32];
  REQUIRE(release::digest_from_checksum_file(hex + "  p64.bin\n", d));
  CHECK_EQ(d[0], 0x3a);
  CHECK_EQ(d[31], 0x1b);
  REQUIRE(release::digest_from_checksum_file("\xEF\xBB\xBF" + hex, d));  // a BOM before the digest
  CHECK_EQ(d[1], 0x7b);
  uint8_t upper[32];
  std::string up = hex;
  for (char &c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  REQUIRE(release::hex_to_bin(up, upper));
  CHECK(std::memcmp(upper, d, 32) == 0);
  CHECK(!release::digest_from_checksum_file("3a7bd3", d));  // too short
  CHECK(!release::hex_to_bin(std::string(64, 'g'), d));      // not hex
}

}  // namespace

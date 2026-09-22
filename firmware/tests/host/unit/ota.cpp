// Host unit tests: the firmware version rule.
#include "common.hpp"

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

}  // namespace

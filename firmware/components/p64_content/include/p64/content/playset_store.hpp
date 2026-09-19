// p64 -- the playset store: user playsets as JSON files under the card's
// channels/playsets/ folder, one per playset, written atomically (spec 5.2, 14). The
// built-in playsets are never stored. Card I/O: core-0 tasks only.
#pragma once

#include <string>
#include <vector>

#include "p64/content/playset.hpp"

namespace p64::content::store {

struct Summary {
  std::string name;
  size_t channels = 0;
};

std::string playsets_dir();
// The user playsets on the card, sorted by name (files that fail to parse are skipped
// and logged). False when the folder cannot be read.
bool list(std::vector<Summary> &out, std::string &error);
bool exists(const std::string &name);
bool load(const std::string &name, Playset &out, std::string &error);
// Validates and writes; refuses a 33rd playset and built-in names.
bool save(const Playset &playset, std::string &error);
bool remove(const std::string &name, std::string &error);

}  // namespace p64::content::store

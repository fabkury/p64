// p64 -- the playset store: user playsets as JSON files under the card's
// channels/playsets/ folder, one per playset, written atomically (spec 5.2, 14). The
// built-in playsets are never stored. Card I/O: core-0 tasks only.
//
// File naming (ADR 0017): a name that fits the legacy rule is stored as <name>.json,
// exactly as before, and the stem stays its identity; any other name is stored as
// <slug>~<hash>.json with the name inside (playset_name.hpp). Names are the same
// playset when their canonical forms match; the spelling on the card wins.
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
// The user playsets on the card, sorted by canonical name (files that fail to parse or
// are not where their name puts them are skipped and logged). False when the folder
// cannot be read.
bool list(std::vector<Summary> &out, std::string &error);
bool exists(const std::string &name);
// Loads by name in any spelling; out.name is the spelling stored on the card.
bool load(const std::string &name, Playset &out, std::string &error);
// Validates and writes; refuses a 33rd playset, built-in names and a hash collision
// with another playset. When the playset already exists under another spelling, the
// stored spelling is kept and written back into playset.name.
bool save(Playset &playset, std::string &error);
bool remove(const std::string &name, std::string &error);

}  // namespace p64::content::store

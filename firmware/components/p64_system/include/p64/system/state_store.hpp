// p64 -- small persisted state that is not a user setting: the active playset name and
// the like. NVS namespace "p64state", string values, keys up to 15 characters.
#pragma once

#include <string>

namespace p64::system::state {

bool get(const char *key, std::string &out);
bool set(const char *key, const std::string &value);
bool erase(const char *key);
// Wipes the namespace (factory reset).
bool erase_all();

}  // namespace p64::system::state

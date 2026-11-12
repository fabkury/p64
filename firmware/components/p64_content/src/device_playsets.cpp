#include "p64/content/device_playsets.hpp"

#include <algorithm>

#include "cJSON.h"
#include "p64/content/playset_json.hpp"
#include "p64/content/playset_name.hpp"

namespace p64::content::device_playsets {
namespace {

std::string slot_key(size_t i) { return "dps" + std::to_string(i); }

struct Slot {
  bool used = false;    // something is stored there
  bool valid = false;   // and it parses as a playset with a valid name
  size_t bytes = 0;
  Playset playset;
};

Slot read_slot(const Backend &b, size_t i) {
  Slot s;
  std::string text;
  if (!b.get(slot_key(i), text) || text.empty()) return s;
  s.used = true;
  s.bytes = text.size();
  cJSON *json = cJSON_ParseWithLength(text.c_str(), text.size());
  if (!json) return s;
  std::string error;
  s.valid = playset_from_json(json, s.playset, error) && valid_playset_name(s.playset.name);
  cJSON_Delete(json);
  s.playset.builtin = false;
  return s;
}

// The slot holding `name` (in any spelling), or -1.
int find(const Backend &b, const std::string &name, Slot *out = nullptr) {
  for (size_t i = 0; i < kMaxPlaysets; ++i) {
    Slot s = read_slot(b, i);
    if (s.valid && same_playset_name(s.playset.name, name)) {
      if (out) *out = std::move(s);
      return static_cast<int>(i);
    }
  }
  return -1;
}

// A slot to write a new playset into: an empty one, else one whose content is unreadable.
int free_slot(const Backend &b) {
  int broken = -1;
  for (size_t i = 0; i < kMaxPlaysets; ++i) {
    const Slot s = read_slot(b, i);
    if (!s.used) return static_cast<int>(i);
    if (!s.valid && broken < 0) broken = static_cast<int>(i);
  }
  return broken;
}

bool fits_device(const Playset &p, std::string &error) {
  if (p.channels.size() > kMaxChannels) {
    error = "a device playset has at most 16 channels (a card holds up to 64)";
    return false;
  }
  for (const ChannelSpec &c : p.channels) {
    if (c.kind == ChannelKind::Local) {
      error = "local channels need a card";
      return false;
    }
  }
  return true;
}

bool serialize(const Playset &p, std::string &text, std::string &error) {
  cJSON *json = playset_to_json(p);
  cJSON_DeleteItemFromObject(json, "builtin");
  char *raw = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (!raw) {
    error = "out of memory";
    return false;
  }
  text = raw;
  cJSON_free(raw);
  if (text.size() > kMaxPlaysetBytes) {
    error = "the playset is too large to keep on the device; shorten its channel names";
    return false;
  }
  return true;
}

}  // namespace

bool list(const Backend &b, std::vector<store::Summary> &out, std::string &error) {
  (void)error;
  out.clear();
  for (size_t i = 0; i < kMaxPlaysets; ++i) {
    const Slot s = read_slot(b, i);
    if (s.valid) out.push_back(store::Summary{s.playset.name, s.playset.channels.size()});
  }
  std::sort(out.begin(), out.end(), [](const store::Summary &x, const store::Summary &y) {
    const std::string cx = canonical_playset_name(x.name), cy = canonical_playset_name(y.name);
    return cx != cy ? cx < cy : x.name < y.name;
  });
  return true;
}

bool exists(const Backend &b, const std::string &name) { return valid_playset_name(name) && find(b, name) >= 0; }

bool load(const Backend &b, const std::string &name, Playset &out, std::string &error) {
  if (!valid_playset_name(name)) {
    error = "invalid playset name";
    return false;
  }
  Slot s;
  if (find(b, name, &s) < 0) {
    error = "no such playset";
    return false;
  }
  out = std::move(s.playset);
  return true;
}

bool save(const Backend &b, Playset &playset, std::string &error) {
  if (playset.builtin) {
    error = "built-in playsets cannot be saved";
    return false;
  }
  if (!playset.validate(error) || !fits_device(playset, error)) return false;
  Slot old;
  int slot = find(b, playset.name, &old);
  if (slot >= 0) {
    playset.name = old.playset.name;  // a save under another spelling is an update, not a rename
  } else {
    std::vector<store::Summary> all;
    std::string e;
    list(b, all, e);
    if (all.size() >= kMaxPlaysets) {
      error = "already 8 playsets on the device (a card holds up to 32)";
      return false;
    }
    slot = free_slot(b);
    if (slot < 0) {
      error = "no room for another playset on the device";
      return false;
    }
  }
  std::string text;
  if (!serialize(playset, text, error)) return false;
  if (bytes_used(b) - (old.used ? old.bytes : 0) + text.size() > kMaxTotalBytes) {
    error = "the device's playsets are full; delete one or shorten its channel names";
    return false;
  }
  if (!b.set(slot_key(static_cast<size_t>(slot)), text)) {
    error = "the device could not store the playset";
    return false;
  }
  return true;
}

bool remove(const Backend &b, const std::string &name, std::string &error) {
  if (!valid_playset_name(name)) {
    error = "invalid playset name";
    return false;
  }
  const int slot = find(b, name);
  if (slot < 0) {
    error = "no such playset";
    return false;
  }
  if (!b.erase(slot_key(static_cast<size_t>(slot)))) {
    error = "the device could not delete the playset";
    return false;
  }
  return true;
}

bool rename(const Backend &b, const std::string &old_name, const std::string &new_name, std::string &error) {
  if (!valid_playset_name(old_name) || !valid_playset_name(new_name)) {
    error = "invalid playset name";
    return false;
  }
  Builtin builtin;
  if (builtin_from_name(new_name, builtin)) {
    error = "that name is reserved for a built-in playset";
    return false;
  }
  Slot s;
  const int slot = find(b, old_name, &s);
  if (slot < 0) {
    error = "no such playset";
    return false;
  }
  if (!same_playset_name(old_name, new_name)) {
    Slot taken;
    if (find(b, new_name, &taken) >= 0) {
      error = "a playset named '" + taken.playset.name + "' already exists";
      return false;
    }
  }
  s.playset.name = new_name;
  std::string text;
  if (!serialize(s.playset, text, error)) return false;
  if (!b.set(slot_key(static_cast<size_t>(slot)), text)) {  // in place: one write, nothing to clean up
    error = "the device could not store the playset";
    return false;
  }
  return true;
}

size_t bytes_used(const Backend &b) {
  size_t total = 0;
  for (size_t i = 0; i < kMaxPlaysets; ++i) {
    std::string text;
    if (b.get(slot_key(i), text)) total += text.size();
  }
  return total;
}

}  // namespace p64::content::device_playsets

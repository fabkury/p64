#include "p64/content/playset_store.hpp"

#include <algorithm>
#include <memory>

#include "cJSON.h"
#include "esp_log.h"
#include "p64/content/playset_json.hpp"
#include "p64/content/playset_name.hpp"
#include "p64/storage/card.hpp"

namespace p64::content::store {
namespace {

constexpr const char *TAG = "playsets";
constexpr size_t kMaxFileBytes = 64 * 1024;

bool parse_file(const std::string &path, Playset &out, std::string &error) {
  std::vector<uint8_t> bytes;
  if (!storage::read_file(path, bytes, kMaxFileBytes, error)) return false;
  bytes.push_back(0);
  cJSON *json = cJSON_ParseWithLength(reinterpret_cast<const char *>(bytes.data()), bytes.size() - 1);
  if (!json) {
    error = "not valid JSON";
    return false;
  }
  const bool ok = playset_from_json(json, out, error);
  cJSON_Delete(json);
  return ok;
}

bool ascii_equal_ignoring_case(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const char x = a[i], y = b[i];
    const char lx = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
    const char ly = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
    if (lx != ly) return false;
  }
  return true;
}

// The stem of a ".json" entry; empty for anything else.
std::string stem_of(const storage::FileInfo &f) {
  if (f.directory || storage::extension_of(f.name) != ".json") return "";
  return f.name.substr(0, f.name.size() - 5);
}

// Where a name lives on the card (ADR 0017). A legacy-shaped name is matched against
// the folder's stems ignoring ASCII case, because FAT would: "MIX" and "Mix" are one
// file, and the listing's spelling is the stored one. A new-style name has one possible
// path; when a file is there, its inner name must be the same playset, otherwise the
// four hex digits collided and the file belongs to someone else.
struct Located {
  std::string path;
  std::string stored_name;  // the spelling on the card (the given one when nothing is stored)
  std::string other_name;   // the inner name of a colliding new-style file
  bool new_style = false;
  bool exists = false;
};

Located locate(const std::string &name) {
  Located l;
  const std::string dir = playsets_dir();
  l.stored_name = name;
  if (legacy_playset_name(name)) {
    l.path = dir + "/" + name + ".json";
    if (!storage::exists(dir)) return l;
    for (const storage::FileInfo &f : storage::list(dir)) {
      const std::string stem = stem_of(f);
      if (!stem.empty() && ascii_equal_ignoring_case(stem, name)) {
        l.path = dir + "/" + f.name;
        l.stored_name = stem;
        l.exists = true;
        break;
      }
    }
    return l;
  }
  l.new_style = true;
  l.path = dir + "/" + playset_file_stem(name) + ".json";
  if (!storage::exists(l.path)) return l;
  Playset p;
  std::string e;
  if (!parse_file(l.path, p, e)) {
    ESP_LOGW(TAG, "%s: unreadable (%s)", l.path.c_str(), e.c_str());
    l.other_name = "?";
    return l;
  }
  if (!same_playset_name(p.name, name)) {
    l.other_name = p.name;
    return l;
  }
  l.stored_name = p.name;
  l.exists = true;
  return l;
}

}  // namespace

std::string playsets_dir() { return storage::channels_dir() + "/playsets"; }

bool list(std::vector<Summary> &out, std::string &error) {
  out.clear();
  if (!storage::mounted()) {
    error = "no card";
    return false;
  }
  const std::string dir = playsets_dir();
  if (!storage::exists(dir)) return true;  // nothing saved yet
  for (const storage::FileInfo &f : storage::list(dir)) {
    const std::string stem = stem_of(f);
    if (stem.empty()) continue;
    const bool legacy = legacy_playset_name(stem);
    if (!legacy && !new_style_stem(stem)) continue;
    Playset p;
    std::string e;
    if (!parse_file(dir + "/" + f.name, p, e)) {
      ESP_LOGW(TAG, "%s: skipped (%s)", f.name.c_str(), e.c_str());
      continue;
    }
    std::string name = stem;  // a legacy file's stem is its identity, whatever it says inside
    if (!legacy) {
      // A new-style file is trusted only when it is where its own name puts it.
      if (!valid_playset_name(p.name) || playset_file_stem(p.name) != stem) {
        ESP_LOGD(TAG, "%s: skipped (not the file of '%s')", f.name.c_str(), p.name.c_str());
        continue;
      }
      name = p.name;
    }
    out.push_back(Summary{name, p.channels.size()});
  }
  std::sort(out.begin(), out.end(), [](const Summary &a, const Summary &b) {
    const std::string ca = canonical_playset_name(a.name), cb = canonical_playset_name(b.name);
    return ca != cb ? ca < cb : a.name < b.name;
  });
  return true;
}

bool exists(const std::string &name) {
  if (!valid_playset_name(name) || !storage::mounted()) return false;
  return locate(name).exists;
}

bool load(const std::string &name, Playset &out, std::string &error) {
  if (!valid_playset_name(name)) {
    error = "invalid playset name";
    return false;
  }
  if (!storage::mounted()) {
    error = "no card";
    return false;
  }
  const Located l = locate(name);
  if (!l.exists) {
    error = "no such playset";
    return false;
  }
  if (!parse_file(l.path, out, error)) return false;
  out.name = l.stored_name;  // one spelling everywhere: the card's
  out.builtin = false;
  return true;
}

bool save(Playset &playset, std::string &error) {
  if (playset.builtin) {
    error = "built-in playsets cannot be saved";
    return false;
  }
  if (!playset.validate(error)) return false;
  if (!storage::mounted()) {
    error = "no card";
    return false;
  }
  const std::string dir = playsets_dir();
  if (!storage::exists(dir) && !storage::make_dir(dir, error)) return false;
  const Located l = locate(playset.name);
  if (!l.other_name.empty()) {
    error = "that name collides with the playset '" + l.other_name + "'; pick another";
    return false;
  }
  if (l.exists) {
    playset.name = l.stored_name;  // a save under another spelling is an update, not a rename
  } else {
    std::vector<Summary> existing;
    std::string e;
    if (list(existing, e) && existing.size() >= kMaxPlaysets) {
      error = "already 32 playsets";
      return false;
    }
  }
  cJSON *json = playset_to_json(playset);
  cJSON_DeleteItemFromObject(json, "builtin");
  if (l.new_style) cJSON_AddNumberToObject(json, "format", 2);
  char *text = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (!text) {
    error = "out of memory";
    return false;
  }
  const bool ok = storage::write_file(l.path, reinterpret_cast<const uint8_t *>(text), std::char_traits<char>::length(text), error);
  cJSON_free(text);
  if (ok) ESP_LOGI(TAG, "saved playset '%s' as %s (%u channels)", playset.name.c_str(), l.path.c_str(), static_cast<unsigned>(playset.channels.size()));
  return ok;
}

bool remove(const std::string &name, std::string &error) {
  if (!valid_playset_name(name)) {
    error = "invalid playset name";
    return false;
  }
  if (!storage::mounted()) {
    error = "no card";
    return false;
  }
  const Located l = locate(name);
  if (!l.exists) {
    error = "no such playset";
    return false;
  }
  return storage::remove_path(l.path, error);
}

}  // namespace p64::content::store

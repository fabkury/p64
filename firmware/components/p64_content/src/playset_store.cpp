#include "p64/content/playset_store.hpp"

#include <algorithm>
#include <memory>

#include "cJSON.h"
#include "esp_log.h"
#include "p64/content/playset_json.hpp"
#include "p64/storage/card.hpp"

namespace p64::content::store {
namespace {

constexpr const char *TAG = "playsets";
constexpr size_t kMaxFileBytes = 64 * 1024;

std::string file_of(const std::string &name) { return playsets_dir() + "/" + name + ".json"; }

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
    if (f.directory || storage::extension_of(f.name) != ".json") continue;
    const std::string name = f.name.substr(0, f.name.size() - 5);
    if (!valid_playset_name(name)) continue;
    Playset p;
    std::string e;
    if (!parse_file(dir + "/" + f.name, p, e)) {
      ESP_LOGW(TAG, "%s: skipped (%s)", f.name.c_str(), e.c_str());
      continue;
    }
    out.push_back(Summary{name, p.channels.size()});
  }
  std::sort(out.begin(), out.end(), [](const Summary &a, const Summary &b) { return a.name < b.name; });
  return true;
}

bool exists(const std::string &name) { return valid_playset_name(name) && storage::exists(file_of(name)); }

bool load(const std::string &name, Playset &out, std::string &error) {
  if (!valid_playset_name(name)) {
    error = "invalid playset name";
    return false;
  }
  if (!storage::mounted()) {
    error = "no card";
    return false;
  }
  if (!storage::exists(file_of(name))) {
    error = "no such playset";
    return false;
  }
  if (!parse_file(file_of(name), out, error)) return false;
  out.name = name;  // the file name is the identity
  out.builtin = false;
  return true;
}

bool save(const Playset &playset, std::string &error) {
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
  if (!storage::exists(file_of(playset.name))) {
    std::vector<Summary> existing;
    std::string e;
    if (list(existing, e) && existing.size() >= kMaxPlaysets) {
      error = "already 32 playsets";
      return false;
    }
  }
  cJSON *json = playset_to_json(playset);
  cJSON_DeleteItemFromObject(json, "builtin");
  char *text = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (!text) {
    error = "out of memory";
    return false;
  }
  const bool ok = storage::write_file(file_of(playset.name), reinterpret_cast<const uint8_t *>(text),
                                      std::char_traits<char>::length(text), error);
  cJSON_free(text);
  if (ok) ESP_LOGI(TAG, "saved playset %s (%u channels)", playset.name.c_str(), static_cast<unsigned>(playset.channels.size()));
  return ok;
}

bool remove(const std::string &name, std::string &error) {
  if (!valid_playset_name(name)) {
    error = "invalid playset name";
    return false;
  }
  if (!storage::exists(file_of(name))) {
    error = "no such playset";
    return false;
  }
  return storage::remove_path(file_of(name), error);
}

}  // namespace p64::content::store

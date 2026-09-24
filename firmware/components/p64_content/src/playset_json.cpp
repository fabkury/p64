#include "p64/content/playset_json.hpp"

namespace p64::content {
namespace {

std::string string_of(const cJSON *obj, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
}

bool number_of(const cJSON *obj, const char *key, uint32_t &out) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!v) return true;  // absent: keep the default
  if (!cJSON_IsNumber(v)) return false;
  const double d = v->valuedouble;
  if (d < 0) {
    out = 0;
  } else if (d > 4294967295.0) {
    out = 4294967295u;
  } else {
    out = static_cast<uint32_t>(d);
  }
  return true;
}

}  // namespace

cJSON *playset_to_json(const Playset &playset) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", playset.name.c_str());
  cJSON_AddBoolToObject(root, "builtin", playset.builtin);
  cJSON *channels = cJSON_AddArrayToObject(root, "channels");
  for (const ChannelSpec &c : playset.channels) {
    cJSON *ch = cJSON_CreateObject();
    cJSON_AddStringToObject(ch, "kind", kind_name(c.kind));
    cJSON_AddStringToObject(ch, "identifier", c.identifier.c_str());
    cJSON_AddStringToObject(ch, "display_name",
                            (c.display_name.empty() ? c.default_display_name() : c.display_name).c_str());
    cJSON_AddNumberToObject(ch, "weight", c.weight);
    cJSON_AddNumberToObject(ch, "offset", c.offset);
    cJSON_AddItemToArray(channels, ch);
  }
  return root;
}

bool playset_from_json(const cJSON *json, Playset &out, std::string &error) {
  out = Playset{};
  if (!cJSON_IsObject(json)) {
    error = "playset must be an object";
    return false;
  }
  out.name = string_of(json, "name");
  const cJSON *channels = cJSON_GetObjectItemCaseSensitive(json, "channels");
  if (!cJSON_IsArray(channels)) {
    error = "'channels' must be an array";
    return false;
  }
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, channels) {
    if (!cJSON_IsObject(item)) {
      error = "every channel must be an object";
      return false;
    }
    ChannelSpec spec;
    std::string kind = string_of(item, "kind");
    const std::string p3a_type = string_of(item, "type");
    const std::string p3a_name = string_of(item, "name");
    if (kind.empty()) kind = p3a_type;
    if (kind.empty()) {
      error = "channel " + std::to_string(out.channels.size() + 1) + ": missing 'kind'";
      return false;
    }
    if (!kind_from_name(kind, p3a_name, spec.kind)) {
      error = "channel " + std::to_string(out.channels.size() + 1) + ": unknown kind '" + kind + "'";
      return false;
    }
    spec.identifier = string_of(item, "identifier");
    spec.display_name = string_of(item, "display_name");
    // The document carries the default name for readers; it is not a name the user chose,
    // so it stays implicit (a provider's own label for an external channel applies then).
    if (spec.display_name == spec.default_display_name()) spec.display_name.clear();
    if (!number_of(item, "weight", spec.weight) || !number_of(item, "offset", spec.offset)) {
      error = "channel " + std::to_string(out.channels.size() + 1) + ": weight and offset must be numbers";
      return false;
    }
    if (spec.identifier.size() > kMaxIdentifier) {
      error = "channel " + std::to_string(out.channels.size() + 1) + ": identifier too long";
      return false;
    }
    out.channels.push_back(std::move(spec));
    if (out.channels.size() > kMaxChannels) {
      error = "more than 64 channels";
      return false;
    }
  }
  return true;
}

}  // namespace p64::content

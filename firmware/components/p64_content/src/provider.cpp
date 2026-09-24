#include "p64/content/provider.hpp"

namespace p64::content::providers {
namespace {
std::vector<Provider *> g_providers;
}

void add(Provider *provider) {
  if (!provider) return;
  for (Provider *p : g_providers) {
    if (p == provider) return;
  }
  g_providers.push_back(provider);
}

void clear() { g_providers.clear(); }

const std::vector<Provider *> &all() { return g_providers; }

Provider *find(const std::string &id) {
  for (Provider *p : g_providers) {
    if (id == p->id()) return p;
  }
  return nullptr;
}

Provider *for_spec(const ChannelSpec &spec) {
  if (spec.kind == ChannelKind::External) return find(spec.provider_id());
  for (Provider *p : g_providers) {
    if (p->owns(spec)) return p;
  }
  return nullptr;
}

bool memory_bytes(const std::string &path, std::vector<uint8_t> &out) {
  for (Provider *p : g_providers) {
    if (p->memory_bytes(path, out)) return true;
  }
  return false;
}

cJSON *status_json() {
  cJSON *arr = cJSON_CreateArray();
  for (Provider *p : g_providers) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", p->id());
    cJSON_AddStringToObject(o, "label", p->label());
    const Provider::State s = p->state();
    cJSON_AddBoolToObject(o, "online", s.online);
    cJSON_AddBoolToObject(o, "authorized", s.authorized);
    cJSON *channels = cJSON_AddArrayToObject(o, "channels");
    for (const ChannelOffer &offer : p->offers()) {
      cJSON *c = cJSON_CreateObject();
      cJSON_AddStringToObject(c, "identifier", offer.identifier.c_str());
      cJSON_AddStringToObject(c, "label", offer.label.c_str());
      cJSON_AddItemToArray(channels, c);
    }
    if (cJSON *own = p->status_json()) cJSON_AddItemToObject(o, "status", own);
    if (const char *path = p->settings_path()) cJSON_AddStringToObject(o, "settings_path", path);
    cJSON_AddItemToArray(arr, o);
  }
  return arr;
}

void erase_credentials() {
  for (Provider *p : g_providers) p->erase_credentials();
}

}  // namespace p64::content::providers

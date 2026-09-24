// p64 -- content providers: the seam between the show and every source of channel
// artworks that is not a folder on the card (ADR 0012). Makapix Club is the first
// provider; the private area (firmware/private) adds its own. The show knows a
// provider only through this interface: which channels it serves, what is playable in
// them right now, where an item's file is, and what happened to the item. Pure header
// plus a registry, no ESP-IDF include, so show_core is host-tested with a fake provider.
//
// Threading: the show calls a provider from the show task (or under the show's mutex);
// providers keep their own locks. A provider tells the show that a channel changed with
// system::publish(Event::ProviderChannelChanged); the show re-reads its snapshots then.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cJSON.h"
#include "p64/content/playset.hpp"
#include "p64/content/psram.hpp"

namespace p64::content {

// A channel as a provider sees it: its kind plus the identifier (for External channels
// the part after "<provider>:").
struct ChannelRef {
  ChannelKind kind = ChannelKind::MakapixPromoted;
  std::string identifier;
};

// One item a channel can play right now. Compact on purpose: a channel snapshot may
// hold thousands of them (PSRAM); the path and the name are resolved at pick time.
struct ProviderItem {
  int32_t id = -1;      // the provider's stable id of the artwork (Makapix: the post id)
  uint16_t width = 0;   // pixels, 0 when the listing did not say (the decoder judges later)
  uint16_t height = 0;
};
using ProviderItems = std::vector<ProviderItem, PsramAllocator<ProviderItem>>;

// The size limit (setting "maximum artwork size"): both sides within `max_side`.
inline bool fits_side(const ProviderItem &e, uint16_t max_side) { return e.width <= max_side && e.height <= max_side; }

struct ChannelSnapshot {
  ProviderItems items;        // playable now (their files are at hand), before the size limit
  uint32_t listed = 0;        // entries the provider knows for the channel (its index)
  uint32_t last_refresh = 0;  // epoch seconds of the last listing, 0 never
  uint32_t oversized = 0;     // listed but over the size limit at the last refresh
  bool refreshing = false;
  std::string error;          // the last refresh failure ("" when fine)
};

// A channel the provider offers to the web UI (External channels only; Makapix channels
// have kinds of their own that the UI knows).
struct ChannelOffer {
  std::string identifier;  // the part after "<provider>:"
  std::string label;
};

class Provider {
 public:
  virtual ~Provider() = default;
  // Short id, [a-z0-9_-]: External identifiers are "<id>:<channel>"; also the JSON name.
  virtual const char *id() const = 0;
  virtual const char *label() const = 0;
  // Which channel specs this provider serves.
  virtual bool owns(const ChannelSpec &spec) const = 0;

  struct State {
    bool online = false;      // it can reach its server right now
    bool authorized = false;  // credentials in place (paired, configured); channels that need them work
  };
  virtual State state() = 0;

  // The show says which of the provider's channels are in the active playset (refreshes
  // and downloads target them); it then reads their snapshots.
  virtual void set_active_channels(const std::vector<ChannelRef> &refs) = 0;
  virtual bool snapshot(const ChannelRef &ref, ChannelSnapshot &out) = 0;
  // An item's file (an absolute card path or "mem:<key>") and display name; false when
  // the item is gone.
  virtual bool resolve(const ChannelRef &ref, const ProviderItem &item, std::string &path, std::string &name) = 0;
  // The show reports what happened: the file was missing or undecodable; an item went up
  // (from one of the provider's channels, or as play-this when `ref` is null); nothing of
  // the provider's is up any more.
  virtual void note_load_failed(const ChannelRef &ref, const ProviderItem &item, bool missing) = 0;
  virtual void note_shown(int32_t id, const ChannelRef *ref, bool play_this) = 0;
  virtual void note_hidden() = 0;

  // "mem:" paths the provider keeps in memory (without a card); false when not its own.
  virtual bool memory_bytes(const std::string &path, std::vector<uint8_t> &out) {
    (void)path;
    (void)out;
    return false;
  }
  // The channels offered to the UI as External channels (may be empty).
  virtual std::vector<ChannelOffer> offers() { return {}; }
  // Provider-specific status for the status document (may be null).
  virtual cJSON *status_json() { return nullptr; }
};

// The registry. Providers are registered at start-up (before the show restores its
// playset) and live for the whole run; the show never owns them.
namespace providers {
void add(Provider *provider);
void clear();  // tests only
const std::vector<Provider *> &all();
Provider *find(const std::string &id);
// The provider serving `spec` (null for local channels and unknown External ids).
Provider *for_spec(const ChannelSpec &spec);
// Asks every provider for a "mem:" path.
bool memory_bytes(const std::string &path, std::vector<uint8_t> &out);
// The `providers` array of the status document: id, label, online, authorized, the
// offered channels and each provider's own status.
cJSON *status_json();
}  // namespace providers

}  // namespace p64::content

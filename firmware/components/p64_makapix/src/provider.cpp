// Makapix Club as a content provider (ADR 0012): the adapter between the generic
// interface the show uses (p64/content/provider.hpp) and this component's channel
// indexes and cache. Items are post ids; the show resolves an item to its cached file at
// pick time and reports loads, shows and hides through here.
#include <cstring>

#include "cache.hpp"
#include "internal.hpp"
#include "p64/content/provider.hpp"
#include "p64/makapix/makapix.hpp"

namespace p64::makapix {
namespace {

// The channel's entry with this post id, copied out under the lock (empty when unknown).
bool find_entry(const ChannelRef &ref, int32_t post_id, content::MakapixEntry &out) {
  std::lock_guard<std::mutex> lock(internal::g_mutex);
  internal::Channel *ch = internal::find_channel(channel_id(ref));
  if (!ch) return false;
  for (const content::MakapixEntry &e : ch->entries) {
    if (e.post_id == post_id) {
      out = e;
      return true;
    }
  }
  return false;
}

class MakapixProvider : public content::Provider {
 public:
  const char *id() const override { return "makapix"; }
  const char *label() const override { return "Makapix Club"; }
  bool owns(const content::ChannelSpec &spec) const override { return spec.is_makapix(); }

  content::Provider::State state() override {
    const Status s = status();
    content::Provider::State out;
    out.online = s.online;
    out.authorized = s.state == makapix::State::Paired;
    return out;
  }

  void set_active_channels(const std::vector<ChannelRef> &refs) override { makapix::set_active_channels(refs); }

  bool snapshot(const ChannelRef &ref, content::ChannelSnapshot &out) override {
    ChannelSnapshot snap;
    if (!makapix::snapshot(ref, snap)) return false;
    out.listed = static_cast<uint32_t>(snap.entries.size());
    out.last_refresh = snap.last_refresh;
    out.oversized = snap.oversized;
    out.refreshing = snap.refreshing;
    out.error = snap.error;
    out.items.clear();
    out.items.reserve(snap.cached);
    for (const content::MakapixEntry &e : snap.entries) {
      if (e.flags & content::kMakapixCached) out.items.push_back({e.post_id, e.width, e.height});
    }
    return true;
  }

  bool resolve(const ChannelRef &ref, const content::ProviderItem &item, std::string &path,
               std::string &name) override {
    content::MakapixEntry e;
    if (!find_entry(ref, item.id, e) || !(e.flags & content::kMakapixCached)) return false;
    path = cache::artwork_path(e);
    name = e.sqid[0] ? std::string(e.sqid) : "post " + std::to_string(e.post_id);
    return true;
  }

  void note_load_failed(const ChannelRef &ref, const content::ProviderItem &item, bool missing) override {
    content::MakapixEntry e;
    if (find_entry(ref, item.id, e)) makapix::note_load_failed(e, missing);
  }
  void note_shown(int32_t id, const ChannelRef *ref, bool play_this) override { makapix::note_shown(id, ref, play_this); }
  void note_hidden() override { makapix::note_hidden(); }

  bool memory_bytes(const std::string &path, std::vector<uint8_t> &out) override {
    if (path.rfind("mem:", 0) != 0) return false;
    return makapix::memory_bytes(path, out);
  }
};

MakapixProvider g_provider;

}  // namespace

content::Provider &provider() { return g_provider; }

}  // namespace p64::makapix

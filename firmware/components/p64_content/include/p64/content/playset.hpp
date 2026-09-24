// p64 -- the content model's value types: channels and playsets (spec section 5,
// glossary "Channel", "Channel kind", "Playset", "Built-in playset"). Pure data plus
// validation; no I/O, no ESP-IDF, so it is host-tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p64::content {

enum class ChannelKind : uint8_t {
  Local = 0,         // a folder under animations/ on the card; identifier = folder ("" = the root)
  MakapixPromoted,   // no identifier; works without pairing
  MakapixAll,        // pairing
  MakapixOwn,        // the owner's posts; pairing
  MakapixArtist,     // identifier = artist sqid; pairing
  MakapixHashtag,    // identifier = tag without '#'; pairing
  MakapixReactions,  // identifier = user sqid; pairing
  UrlList,           // reserved for v1.x
  Pinned,            // reserved for v1.x
  External,          // a channel of a registered content provider; identifier = "<provider>:<channel>" (ADR 0012)
};

constexpr size_t kMaxPlaysets = 32;
constexpr size_t kMaxChannels = 64;
constexpr size_t kMaxPlaysetName = 32;
constexpr size_t kMaxDisplayName = 64;
constexpr size_t kMaxIdentifier = 64;
constexpr uint32_t kMaxWeight = 1000000;
constexpr uint32_t kMaxOffset = 1u << 30;

struct ChannelSpec {
  ChannelKind kind = ChannelKind::Local;
  std::string identifier;    // meaning depends on the kind (see above)
  std::string display_name;  // up to 64 characters; empty = default_display_name()
  uint32_t weight = 0;       // 0 mutes when others are set; all zero = equal shares
  uint32_t offset = 0;       // recency cursor start for ordered sources (local, URL list, pinned)

  std::string default_display_name() const;
  bool is_makapix() const { return kind >= ChannelKind::MakapixPromoted && kind <= ChannelKind::MakapixReactions; }
  bool is_external() const { return kind == ChannelKind::External; }
  // External channels: the provider id before the ':' and the channel after it ("" otherwise).
  std::string provider_id() const;
  std::string provider_channel() const;
  bool needs_card() const { return kind == ChannelKind::Local || kind == ChannelKind::Pinned; }
  bool needs_network() const { return is_makapix() || is_external() || kind == ChannelKind::UrlList; }
  bool needs_pairing() const { return is_makapix() && kind != ChannelKind::MakapixPromoted; }
  bool supported() const { return kind != ChannelKind::UrlList && kind != ChannelKind::Pinned; }
  // Kinds whose entries have a stable order (offset applies; recency starts there).
  bool ordered() const { return kind == ChannelKind::Local || kind == ChannelKind::UrlList || kind == ChannelKind::Pinned; }
  // Field-level validation (identifier shape, lengths, ranges).
  bool validate(std::string &error) const;
};

struct Playset {
  std::string name;  // 1..32 of [A-Za-z0-9_] for user playsets; built-ins use their display names
  std::vector<ChannelSpec> channels;  // 1..64
  bool builtin = false;

  // Validates the name (user playsets), the channel count and every channel.
  bool validate(std::string &error) const;
  // True when every channel's weight is zero (equal shares).
  bool equal_weights() const;
};

const char *kind_name(ChannelKind kind);  // "local", "promoted", "all", "own", "artist", "hashtag", "reactions", ..., "external"
// Accepts p64's names and p3a's ("sdcard", "user", "named" with a sub-name).
bool kind_from_name(const std::string &name, const std::string &sub_name, ChannelKind &out);
bool valid_playset_name(const std::string &name);
bool valid_sqid(const std::string &s);
bool valid_hashtag(const std::string &s);
bool valid_folder_name(const std::string &s);  // one path segment, no dots-only, no control characters
bool valid_provider_id(const std::string &s);          // 1..16 of [a-z0-9_-]
bool valid_external_identifier(const std::string &s);  // "<provider>:<channel>", channel 1..47 of [A-Za-z0-9_.:/-]

// Built-in playsets (spec 5.2). Their names are reserved (case-insensitively) for users.
enum class Builtin : uint8_t { Promoted = 0, All, Followed, Local };
constexpr size_t kBuiltinCount = 4;
const char *builtin_name(Builtin b);
bool builtin_from_name(const std::string &name, Builtin &out);
// Synthesises a built-in playset. Local gets one channel per folder given (the root
// folder first, as ""), Followed is empty until the server delivers it (M6).
Playset builtin_playset(Builtin b, const std::vector<std::string> &local_folders);

}  // namespace p64::content

#include "p64/content/playset.hpp"

#include <cctype>

namespace p64::content {
namespace {

bool ascii_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

bool same_ignoring_case(const std::string &a, const char *b) {
  size_t i = 0;
  for (; i < a.size() && b[i]; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return i == a.size() && b[i] == '\0';
}

}  // namespace

const char *kind_name(ChannelKind kind) {
  switch (kind) {
    case ChannelKind::Local: return "local";
    case ChannelKind::MakapixPromoted: return "promoted";
    case ChannelKind::MakapixAll: return "all";
    case ChannelKind::MakapixOwn: return "own";
    case ChannelKind::MakapixArtist: return "artist";
    case ChannelKind::MakapixHashtag: return "hashtag";
    case ChannelKind::MakapixReactions: return "reactions";
    case ChannelKind::UrlList: return "url_list";
    case ChannelKind::Pinned: return "pinned";
  }
  return "unknown";
}

bool kind_from_name(const std::string &name, const std::string &sub_name, ChannelKind &out) {
  if (name == "local" || name == "sdcard") {
    out = ChannelKind::Local;
  } else if (name == "promoted") {
    out = ChannelKind::MakapixPromoted;
  } else if (name == "all") {
    out = ChannelKind::MakapixAll;
  } else if (name == "own") {
    out = ChannelKind::MakapixOwn;
  } else if (name == "artist" || name == "user") {
    out = ChannelKind::MakapixArtist;
  } else if (name == "hashtag") {
    out = ChannelKind::MakapixHashtag;
  } else if (name == "reactions") {
    out = ChannelKind::MakapixReactions;
  } else if (name == "url_list") {
    out = ChannelKind::UrlList;
  } else if (name == "pinned") {
    out = ChannelKind::Pinned;
  } else if (name == "named") {  // p3a: {"type":"named","name":"all"|"promoted"}
    if (sub_name == "promoted") {
      out = ChannelKind::MakapixPromoted;
    } else if (sub_name == "all") {
      out = ChannelKind::MakapixAll;
    } else {
      return false;
    }
  } else {
    return false;
  }
  return true;
}

bool valid_playset_name(const std::string &name) {
  if (name.empty() || name.size() > kMaxPlaysetName) return false;
  for (char c : name) {
    if (!ascii_alnum(c) && c != '_') return false;
  }
  return true;
}

bool valid_sqid(const std::string &s) {
  if (s.empty() || s.size() > 32) return false;
  for (char c : s) {
    if (!ascii_alnum(c)) return false;
  }
  return true;
}

bool valid_hashtag(const std::string &s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s) {
    if (!ascii_alnum(c) && c != '_') return false;
  }
  return true;
}

bool valid_folder_name(const std::string &s) {
  if (s.empty() || s.size() > kMaxIdentifier) return false;
  if (s == "." || s == "..") return false;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f || c == '/' || c == '\\') return false;
  }
  return true;
}

std::string ChannelSpec::default_display_name() const {
  switch (kind) {
    case ChannelKind::Local: return identifier.empty() ? "animations" : identifier;
    case ChannelKind::MakapixPromoted: return "Promoted";
    case ChannelKind::MakapixAll: return "All";
    case ChannelKind::MakapixOwn: return "Own";
    case ChannelKind::MakapixArtist: return "Artist " + identifier;
    case ChannelKind::MakapixHashtag: return "#" + identifier;
    case ChannelKind::MakapixReactions: return "Reactions of " + identifier;
    case ChannelKind::UrlList: return "URL list " + identifier;
    case ChannelKind::Pinned: return "Pinned " + identifier;
  }
  return "";
}

bool ChannelSpec::validate(std::string &error) const {
  switch (kind) {
    case ChannelKind::Local:
      if (!identifier.empty() && !valid_folder_name(identifier)) {
        error = "local channel: invalid folder name";
        return false;
      }
      break;
    case ChannelKind::MakapixPromoted:
    case ChannelKind::MakapixAll:
    case ChannelKind::MakapixOwn:
      if (!identifier.empty()) {
        error = std::string(kind_name(kind)) + " channel takes no identifier";
        return false;
      }
      break;
    case ChannelKind::MakapixArtist:
    case ChannelKind::MakapixReactions:
      if (!valid_sqid(identifier)) {
        error = std::string(kind_name(kind)) + " channel: identifier must be a sqid";
        return false;
      }
      break;
    case ChannelKind::MakapixHashtag:
      if (!valid_hashtag(identifier)) {
        error = "hashtag channel: identifier must be a tag without '#'";
        return false;
      }
      break;
    case ChannelKind::UrlList:
    case ChannelKind::Pinned:
      error = std::string(kind_name(kind)) + " channels are not supported yet";
      return false;
  }
  if (display_name.size() > kMaxDisplayName) {
    error = "display name longer than 64 characters";
    return false;
  }
  for (char c : display_name) {
    if (static_cast<unsigned char>(c) < 0x20) {
      error = "display name contains control characters";
      return false;
    }
  }
  if (weight > kMaxWeight) {
    error = "weight above 1000000";
    return false;
  }
  if (offset > kMaxOffset) {
    error = "offset too large";
    return false;
  }
  return true;
}

bool Playset::validate(std::string &error) const {
  if (!builtin && !valid_playset_name(name)) {
    error = "playset name must be 1 to 32 letters, digits or underscores";
    return false;
  }
  Builtin b;
  if (!builtin && builtin_from_name(name, b)) {
    error = "that name is reserved for a built-in playset";
    return false;
  }
  if (channels.empty()) {
    error = "a playset needs at least one channel";
    return false;
  }
  if (channels.size() > kMaxChannels) {
    error = "more than 64 channels";
    return false;
  }
  for (size_t i = 0; i < channels.size(); ++i) {
    std::string e;
    if (!channels[i].validate(e)) {
      error = "channel " + std::to_string(i + 1) + ": " + e;
      return false;
    }
  }
  return true;
}

bool Playset::equal_weights() const {
  for (const ChannelSpec &c : channels) {
    if (c.weight != 0) return false;
  }
  return true;
}

const char *builtin_name(Builtin b) {
  switch (b) {
    case Builtin::Promoted: return "Promoted";
    case Builtin::All: return "All";
    case Builtin::Followed: return "Followed";
    case Builtin::Local: return "Local";
  }
  return "";
}

bool builtin_from_name(const std::string &name, Builtin &out) {
  for (size_t i = 0; i < kBuiltinCount; ++i) {
    const Builtin b = static_cast<Builtin>(i);
    if (same_ignoring_case(name, builtin_name(b))) {
      out = b;
      return true;
    }
  }
  return false;
}

Playset builtin_playset(Builtin b, const std::vector<std::string> &local_folders) {
  Playset p;
  p.builtin = true;
  p.name = builtin_name(b);
  switch (b) {
    case Builtin::Promoted: p.channels.push_back(ChannelSpec{ChannelKind::MakapixPromoted, "", "", 0, 0}); break;
    case Builtin::All: p.channels.push_back(ChannelSpec{ChannelKind::MakapixAll, "", "", 0, 0}); break;
    case Builtin::Followed: break;  // filled by the Makapix layer from the server's followed_artists
    case Builtin::Local:
      for (const std::string &folder : local_folders) {
        if (p.channels.size() >= kMaxChannels) break;
        p.channels.push_back(ChannelSpec{ChannelKind::Local, folder, "", 0, 0});
      }
      if (p.channels.empty()) p.channels.push_back(ChannelSpec{ChannelKind::Local, "", "", 0, 0});
      break;
  }
  return p;
}

}  // namespace p64::content

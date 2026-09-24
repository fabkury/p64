// p64 -- History: the last 32 items shown, with a position (spec 4.6, glossary
// "History", "Previous / Next"). Memory only. Pure data, host-tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p64::content {

enum class ItemKind : uint8_t { Artwork = 0, Interlude = 1 };
// Where an item came from.
enum class Source : uint8_t { Channel = 0, PlayThisFile, PlayThisUrl, PlayThisMakapix };

struct HistoryItem {
  ItemKind kind = ItemKind::Artwork;
  Source source = Source::Channel;
  std::string path;         // absolute file path (artworks), or "mem:<key>" for the memory cache
  std::string name;         // what the UI shows: file name or title
  std::string channel;      // channel display name ("" for play-this)
  int channel_index = -1;   // index in the playset at pick time, -1 for play-this
  int entry_index = -1;     // index in the channel's entries at pick time
  uint8_t channel_kind = 0;         // content::ChannelKind of the channel (providers' views need it)
  std::string channel_identifier;   // the channel's identifier (sqid, tag, folder, provider channel)
  std::string playset;      // active playset name at pick time
  std::string provider;     // the content provider the item came from ("" for local files and plain URLs)
  int32_t item_id = -1;     // the provider's item id (Makapix: the post id), -1 for anything else
  std::string sqid;         // Makapix public sqid when known
  uint8_t widget = 0;       // interludes: which widget
  int64_t shown_at_us = 0;  // when it went up (monotonic)
};

class History {
 public:
  static constexpr size_t kCapacity = 32;

  // Appends after the current position (items ahead of it are discarded, as in a
  // browser), drops the oldest beyond the capacity, and moves the position to the item.
  void push(HistoryItem item);
  bool can_back() const { return !items_.empty() && position_ > 0; }
  bool can_forward() const { return !items_.empty() && position_ + 1 < items_.size(); }
  bool back();
  bool forward();
  bool go_to(size_t index);
  // Drops an item (its file vanished); the position stays on the same neighbour.
  void remove(size_t index);
  void clear();

  const HistoryItem *current() const { return items_.empty() ? nullptr : &items_[position_]; }
  HistoryItem *current() { return items_.empty() ? nullptr : &items_[position_]; }
  size_t size() const { return items_.size(); }
  size_t position() const { return position_; }
  const HistoryItem &at(size_t i) const { return items_[i]; }

 private:
  std::vector<HistoryItem> items_;  // oldest first
  size_t position_ = 0;
};

}  // namespace p64::content

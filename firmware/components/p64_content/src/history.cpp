#include "p64/content/history.hpp"

namespace p64::content {

void History::push(HistoryItem item) {
  if (!items_.empty() && position_ + 1 < items_.size()) items_.resize(position_ + 1);
  items_.push_back(std::move(item));
  if (items_.size() > kCapacity) items_.erase(items_.begin());
  position_ = items_.size() - 1;
}

bool History::back() {
  if (!can_back()) return false;
  --position_;
  return true;
}

bool History::forward() {
  if (!can_forward()) return false;
  ++position_;
  return true;
}

bool History::go_to(size_t index) {
  if (index >= items_.size()) return false;
  position_ = index;
  return true;
}

void History::remove(size_t index) {
  if (index >= items_.size()) return;
  items_.erase(items_.begin() + static_cast<long>(index));
  if (items_.empty()) {
    position_ = 0;
  } else if (position_ > index || position_ >= items_.size()) {
    --position_;
  }
}

void History::clear() {
  items_.clear();
  position_ = 0;
}

}  // namespace p64::content

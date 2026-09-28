// p64 -- the TLS slot's queue (tls_slot.hpp).
#include "tls_slot.hpp"

#include <algorithm>

namespace p64::net::tls_slot {

bool Queue::request(Id id) {
  if (!id) return false;
  if (holder_ == nullptr) {
    holder_ = id;
    depth_ = 1;
    ++stats_.grants;
    return true;
  }
  if (holder_ == id) {
    ++depth_;
    ++stats_.grants;
    return true;
  }
  if (std::find(queue_.begin(), queue_.end(), id) == queue_.end()) {
    queue_.push_back(id);
    ++stats_.waits;
  }
  return false;
}

Id Queue::release(Id id) {
  if (!id || holder_ != id || depth_ == 0) return nullptr;
  if (--depth_ > 0) return nullptr;
  if (queue_.empty()) {
    holder_ = nullptr;
    return nullptr;
  }
  holder_ = queue_.front();
  queue_.erase(queue_.begin());
  depth_ = 1;
  ++stats_.handoffs;
  return holder_;
}

}  // namespace p64::net::tls_slot

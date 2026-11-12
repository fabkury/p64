#include "p64/content/memory_window.hpp"

#include <algorithm>

namespace p64::content {

MemoryWindow::MemoryWindow(size_t files, size_t bytes) : capacity_files_(files), capacity_bytes_(bytes) {}

// Among the kProtected most recently played. Lock held.
bool MemoryWindow::is_protected(const File &f) const {
  if (f.played == 0) return false;
  size_t newer = 0;
  for (const File &o : files_) {
    if (o.played > f.played) ++newer;
  }
  return newer < kProtected;
}

void MemoryWindow::note_filled() { filled_ = true; }

bool MemoryWindow::put(const std::string &key, std::vector<uint8_t> &&bytes) {
  std::vector<std::string> evicted;
  bool kept = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t size = bytes.size();
    // A file of the same key is replaced, not evicted: nobody is told.
    for (auto it = files_.begin(); it != files_.end(); ++it) {
      if (it->key == key) {
        bytes_ -= it->bytes->size();
        files_.erase(it);
        break;
      }
    }
    if (size <= capacity_bytes_ && capacity_files_ > 0) {
      // Which files would have to go: the least recently played first, never a protected one.
      std::vector<size_t> order(files_.size());
      for (size_t i = 0; i < order.size(); ++i) order[i] = i;
      std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return files_[a].order < files_[b].order; });
      std::vector<size_t> going;
      size_t files = files_.size(), total = bytes_;
      for (size_t i : order) {
        if (files < capacity_files_ && total + size <= capacity_bytes_) break;
        if (is_protected(files_[i])) continue;
        going.push_back(i);
        --files;
        total -= files_[i].bytes->size();
      }
      if (files < capacity_files_ && total + size <= capacity_bytes_) {
        std::sort(going.rbegin(), going.rend());
        for (size_t i : going) {
          evicted.push_back(files_[i].key);
          bytes_ -= files_[i].bytes->size();
          files_.erase(files_.begin() + static_cast<std::ptrdiff_t>(i));
        }
        File f;
        f.key = key;
        f.bytes = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
        f.order = ++clock_;
        files_.push_back(std::move(f));
        bytes_ += size;
        ++stats_.stored;
        stats_.evictions += static_cast<uint32_t>(evicted.size());
        kept = true;
        if (!evicted.empty()) note_filled();
        // Full to the brim without an eviction yet: the next file would evict.
        if (files_.size() >= capacity_files_ || bytes_ + capacity_bytes_ / 8 > capacity_bytes_) note_filled();
      }
    }
    if (!kept) ++stats_.refused;
    if (filled_ && swap_pending_) {  // the swap's one download is spent, kept or not
      swap_pending_ = false;
    }
  }
  for (const std::string &k : evicted) {
    for (const auto &listener : listeners_) listener(k);
  }
  return kept;
}

bool MemoryWindow::has(const std::string &key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const File &f : files_) {
    if (f.key == key) return true;
  }
  return false;
}

FileBytes MemoryWindow::take(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (File &f : files_) {
    if (f.key != key) continue;
    f.order = f.played = ++clock_;
    ++stats_.hits;
    return f.bytes;
  }
  ++stats_.misses;
  return nullptr;
}

void MemoryWindow::remove(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = files_.begin(); it != files_.end(); ++it) {
    if (it->key != key) continue;
    bytes_ -= it->bytes->size();
    files_.erase(it);
    return;
  }
}

void MemoryWindow::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  files_.clear();
  bytes_ = 0;
  filled_ = false;
  swap_pending_ = false;
}

void MemoryWindow::note_swap(int64_t now_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!filled_) return;
  if (renewed_once_ && now_us - last_renewal_us_ < kRenewEveryUs) return;
  if (!swap_pending_) {
    swap_pending_ = true;
    renewed_once_ = true;
    last_renewal_us_ = now_us;
    ++stats_.renewals;
  }
}

bool MemoryWindow::admits_download(int64_t) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !filled_ || swap_pending_;
}

void MemoryWindow::on_evicted(std::function<void(const std::string &key)> listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_.push_back(std::move(listener));
}

MemoryWindow::Stats MemoryWindow::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Stats s = stats_;
  s.files = files_.size();
  s.bytes = bytes_;
  s.capacity_files = capacity_files_;
  s.capacity_bytes = capacity_bytes_;
  s.filled = filled_;
  return s;
}

MemoryWindow &memory_window() {
  static MemoryWindow window;
  return window;
}

}  // namespace p64::content

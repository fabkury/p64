#include "cache.hpp"
#include "policy.hpp"

#include <sys/stat.h>

#include <deque>
#include <mutex>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p64/content/psram.hpp"
#include "p64/storage/card.hpp"

namespace p64::makapix::cache {
namespace {

constexpr const char *TAG = "makapix";
constexpr size_t kMemoryCacheBytes = 6 * 1024 * 1024;  // without a card: what PSRAM lends the show
constexpr size_t kMemoryCacheFiles = 48;

struct MemoryFile {
  std::string key;
  std::vector<uint8_t> bytes;
};

std::mutex g_mem_mutex;
std::deque<MemoryFile> g_memory;  // oldest first
size_t g_memory_bytes = 0;

void memory_store(const std::string &key, const std::vector<uint8_t> &bytes) {
  std::lock_guard<std::mutex> lock(g_mem_mutex);
  for (auto it = g_memory.begin(); it != g_memory.end(); ++it) {
    if (it->key == key) {
      g_memory_bytes -= it->bytes.size();
      g_memory.erase(it);
      break;
    }
  }
  while (!g_memory.empty() && (g_memory_bytes + bytes.size() > kMemoryCacheBytes || g_memory.size() >= kMemoryCacheFiles)) {
    g_memory_bytes -= g_memory.front().bytes.size();
    g_memory.pop_front();
  }
  g_memory.push_back(MemoryFile{key, bytes});
  g_memory_bytes += bytes.size();
}

bool memory_has(const std::string &key) {
  std::lock_guard<std::mutex> lock(g_mem_mutex);
  for (const MemoryFile &f : g_memory) {
    if (f.key == key) return true;
  }
  return false;
}

std::string file_key(const content::MakapixEntry &e) {
  return content::format_uuid(e.storage_key) + content::makapix_format_ext(static_cast<content::MakapixFormat>(e.format));
}

bool ensure_dir(const std::string &dir) {
  if (storage::exists(dir)) return true;
  std::string error;
  return storage::make_dir(dir, error);
}

}  // namespace

bool has_card() { return storage::mounted(); }

std::string index_path(const std::string &channel_id) { return storage::channels_dir() + "/" + channel_id + ".p64x"; }

bool load_index(const std::string &channel_id, content::MakapixEntries &out, uint32_t &last_refresh) {
  out.clear();
  last_refresh = 0;
  if (!has_card()) return false;
  const std::string path = index_path(channel_id);
  std::vector<uint8_t> bytes;
  std::string error;
  if (!storage::read_file(path, bytes, 512 * 1024, error)) return false;
  if (!content::deserialize_index(bytes.data(), bytes.size(), out, error)) {
    ESP_LOGW(TAG, "%s: %s; dropped", path.c_str(), error.c_str());
    storage::remove_path(path, error);
    return false;
  }
  struct stat st = {};
  if (stat(path.c_str(), &st) == 0 && st.st_mtime > 0) last_refresh = static_cast<uint32_t>(st.st_mtime);
  return true;
}

bool save_index(const std::string &channel_id, const content::MakapixEntries &entries, std::string &error) {
  if (!has_card()) return false;
  if (!ensure_dir(storage::channels_dir())) {
    error = "no channels folder";
    return false;
  }
  const std::vector<uint8_t> bytes = content::serialize_index(entries);
  return storage::write_file(index_path(channel_id), bytes.data(), bytes.size(), error);
}

bool remove_index(const std::string &channel_id) {
  std::string error;
  return has_card() && storage::remove_path(index_path(channel_id), error);
}

std::string artwork_path(const content::MakapixEntry &e) {
  if (!has_card()) return "mem:" + file_key(e);
  return storage::root() + "/" + content::cache_relative_path(e);
}

bool artwork_present(const content::MakapixEntry &e) {
  if (!has_card()) return memory_has(file_key(e));
  return storage::exists(artwork_path(e));
}

bool store_artwork(const content::MakapixEntry &e, const std::vector<uint8_t> &bytes, std::string &error) {
  if (!has_card()) {
    memory_store(file_key(e), bytes);
    return true;
  }
  const std::string path = artwork_path(e);
  const std::string dir = path.substr(0, path.rfind('/'));
  if (!ensure_dir(storage::cache_dir()) || !ensure_dir(dir)) {
    error = "cannot create the cache folder";
    return false;
  }
  return storage::write_file(path, bytes.data(), bytes.size(), error);
}

std::string store_download(const std::string &name, const std::vector<uint8_t> &bytes, std::string &error) {
  if (!has_card()) {
    const std::string key = "dl-" + name;
    memory_store(key, bytes);
    return "mem:" + key;
  }
  if (!ensure_dir(storage::downloads_dir())) {
    error = "cannot create the downloads folder";
    return "";
  }
  const std::string path = storage::downloads_dir() + "/" + name;
  if (!storage::write_file(path, bytes.data(), bytes.size(), error)) return "";
  return path;
}

bool memory_bytes(const std::string &path, std::vector<uint8_t> &out) {
  if (path.rfind("mem:", 0) != 0) return false;
  const std::string key = path.substr(4);
  std::lock_guard<std::mutex> lock(g_mem_mutex);
  for (const MemoryFile &f : g_memory) {
    if (f.key == key) {
      out = f.bytes;
      return true;
    }
  }
  return false;
}

uint64_t free_bytes() { return has_card() ? storage::info().free : 0; }

void remove_artwork(const content::MakapixEntry &e) {
  std::string error;
  if (has_card()) storage::remove_path(artwork_path(e), error);
}

namespace {

enum class Folder : uint8_t { Cache, Downloads, Channels };

struct SweptFile {
  std::string dir;   // the folder (a shard of cache/, downloads/ or channels/)
  std::string name;  // the file name in it
  size_t size;
  Folder kind;
  int64_t mtime;
};
using SweptFiles = std::vector<SweptFile, content::PsramAllocator<SweptFile>>;

// Every file of cache/ (its shards), downloads/ and channels/, read once from the
// directory entries (name, size, mtime come with the listing: a stat() per file was a
// directory search per file and made a shard of 1900 files cost minutes, 2026-09-29;
// walking twice, once to judge the dates and once to delete, cost 22 s for 8190 files).
// Yields between shards so downloads and refreshes get their turn. In PSRAM: thousands.
void collect_swept_files(SweptFiles &out) {
  const auto folder = [&](const std::string &dir, Folder kind) {
    for (storage::FileInfo &f : storage::list(dir)) {
      if (f.directory) continue;
      out.push_back(SweptFile{dir, std::move(f.name), f.size, kind, f.mtime});
    }
  };
  const std::string cache = storage::cache_dir();
  for (const storage::FileInfo &shard : storage::list(cache)) {
    if (!shard.directory) continue;
    folder(cache + "/" + shard.name, Folder::Cache);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  folder(storage::downloads_dir(), Folder::Downloads);
  folder(storage::channels_dir(), Folder::Channels);
}

}  // namespace

bool sweep(int64_t now, uint32_t older_than_s, int64_t floor, bool dry_run, SweepStats &stats,
           const std::function<void(const std::string &name)> &on_artwork_deleted, std::string &error) {
  if (!has_card()) return false;
  SweptFiles files;
  collect_swept_files(files);
  // First the dates: a file from the future means the clock or the card is not what it
  // seems; then nothing is deleted at all.
  for (const SweptFile &f : files) {
    if (policy::sweep_verdict(f.mtime, now, older_than_s, floor) != policy::SweepVerdict::Suspect) continue;
    error = f.dir + "/" + f.name + " is dated " + std::to_string(f.mtime - now) + " s in the future; nothing deleted";
    return false;
  }
  // Then count and delete.
  for (const SweptFile &f : files) {
    ++stats.examined;
    stats.bytes += f.size;
    if (policy::sweep_verdict(f.mtime, now, older_than_s, floor) != policy::SweepVerdict::Delete) continue;
    const std::string path = f.dir + "/" + f.name;
    if (!dry_run) {
      std::string e;
      if (!storage::remove_path(path, e)) {  // e.g. open by the loader this instant: next night
        ESP_LOGW(TAG, "sweep: %s not removed: %s", path.c_str(), e.c_str());
        continue;
      }
    }
    ++stats.deleted;
    stats.freed += f.size;
    if (f.kind == Folder::Channels) ++stats.indexes_deleted;
    if (f.kind == Folder::Downloads) ++stats.downloads_deleted;
    if (f.kind == Folder::Cache) on_artwork_deleted(f.name);
  }
  return true;
}

}  // namespace p64::makapix::cache

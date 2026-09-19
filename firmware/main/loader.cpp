#include "loader.hpp"

#include <atomic>
#include <cerrno>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "p64/storage/card.hpp"

namespace p64::loader {
namespace {

constexpr const char *TAG = "loader";
constexpr size_t kMaxEntriesPerChannel = 4096;   // spec 17: channel index cap
constexpr size_t kMaxEntriesPerPlayset = 16384;  // memory guard across a playset's local channels

enum class Kind : uint8_t { Load, Scan };

struct Request {
  Kind kind;
  uint32_t id;
  std::string path;
  gfx::Rgb background;
  uint32_t generation;
  content::Playset playset;
};

QueueHandle_t g_queue = nullptr;
LoadCallback g_on_load;
ScanCallback g_on_scan;
std::atomic<uint32_t> g_next_id{1};

void do_load(Request &r) {
  auto *res = new LoadResult;
  res->id = r.id;
  res->path = r.path;
  const int64_t t0 = esp_timer_get_time();
  std::vector<uint8_t> bytes;
  std::string error;
  if (!storage::read_file(r.path, bytes, playback::kMaxFileBytes, error)) {
    struct stat st = {};
    res->missing = stat(r.path.c_str(), &st) != 0;
    res->error = error;
    g_on_load(res);
    return;
  }
  const int64_t t1 = esp_timer_get_time();
  res->bytes = bytes.size();
  auto art = std::make_shared<playback::Artwork>();
  const std::string name = r.path.substr(r.path.rfind('/') + 1);
  if (!art->open(std::move(bytes), name, r.background, error)) {
    res->error = error;
    g_on_load(res);
    return;
  }
  const int64_t t2 = esp_timer_get_time();
  res->artwork = std::move(art);
  res->read_ms = static_cast<uint32_t>((t1 - t0) / 1000);
  res->open_ms = static_cast<uint32_t>((t2 - t1) / 1000);
  g_on_load(res);
}

void do_scan(Request &r) {
  auto *res = new ScanResult;
  res->generation = r.generation;
  const int64_t t0 = esp_timer_get_time();
  content::Playset playset = std::move(r.playset);
  const bool mounted = storage::mounted();
  content::Builtin b;
  if (playset.builtin && content::builtin_from_name(playset.name, b) && b == content::Builtin::Local) {
    std::vector<std::string> folders{""};
    std::vector<std::string> subs, error;
    std::string e;
    if (mounted && content::list_subfolders(storage::animations_dir(), subs, e)) {
      folders.insert(folders.end(), subs.begin(), subs.end());
    }
    playset = content::builtin_playset(content::Builtin::Local, folders);
  }
  res->entries.resize(playset.channels.size());
  res->errors.resize(playset.channels.size());
  size_t total = 0;
  for (size_t i = 0; i < playset.channels.size(); ++i) {
    const content::ChannelSpec &spec = playset.channels[i];
    if (spec.kind != content::ChannelKind::Local) continue;
    if (!mounted) {
      res->errors[i] = "no card";
      continue;
    }
    const std::string dir =
        spec.identifier.empty() ? storage::animations_dir() : storage::animations_dir() + "/" + spec.identifier;
    const size_t cap = total >= kMaxEntriesPerPlayset ? 0 : std::min(kMaxEntriesPerChannel, kMaxEntriesPerPlayset - total);
    uint32_t skipped = 0;
    std::string error;
    if (!content::scan_folder(dir, cap, res->entries[i], error, &skipped)) res->errors[i] = error;
    res->skipped += skipped;
    total += res->entries[i].size();
  }
  res->playset = std::move(playset);
  res->took_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
  g_on_scan(res);
}

void task(void *) {
  while (true) {
    Request *r = nullptr;
    if (xQueueReceive(g_queue, &r, portMAX_DELAY) != pdTRUE || !r) continue;
    if (r->kind == Kind::Load) {
      do_load(*r);
    } else {
      do_scan(*r);
    }
    delete r;
  }
}

bool submit(Request *r) {
  if (!g_queue || xQueueSend(g_queue, &r, pdMS_TO_TICKS(500)) != pdTRUE) {
    ESP_LOGW(TAG, "queue full; request dropped");
    delete r;
    return false;
  }
  return true;
}

}  // namespace

bool start(LoadCallback on_load, ScanCallback on_scan) {
  if (g_queue) return true;
  g_on_load = std::move(on_load);
  g_on_scan = std::move(on_scan);
  g_queue = xQueueCreate(8, sizeof(Request *));
  if (!g_queue) return false;
  // Card I/O and decoder set-up only; the stack can live in PSRAM.
  const BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(task, "loader", 8192, nullptr, 4, nullptr, 0,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ok == pdPASS;
}

uint32_t load(const std::string &path, gfx::Rgb background) {
  uint32_t id = g_next_id.fetch_add(1);
  if (id == 0) id = g_next_id.fetch_add(1);
  auto *r = new Request{Kind::Load, id, path, background, 0, {}};
  return submit(r) ? id : 0;
}

void scan(uint32_t generation, const content::Playset &playset) {
  auto *r = new Request{Kind::Scan, 0, "", gfx::kBlack, generation, playset};
  submit(r);
}

}  // namespace p64::loader

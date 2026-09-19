#include "p64/storage/card.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

namespace p64::storage {
namespace {

constexpr const char *TAG = "card";
constexpr const char *kMountPoint = "/sdcard";
constexpr const char *kRootName = "p64";
constexpr size_t kMaxNameChars = 128;

std::mutex g_mutex;  // mount state
sdmmc_card_t *g_card = nullptr;
std::string g_error;

void ensure_dir(const std::string &path) {
  struct stat st = {};
  if (stat(path.c_str(), &st) == 0) return;
  if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "cannot create %s: %s", path.c_str(), std::strerror(errno));
  }
}

// Mounts under g_mutex.
bool do_mount() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = CONFIG_P64_SD_FREQ_KHZ;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = static_cast<gpio_num_t>(CONFIG_P64_SD_PIN_CLK);
  slot.cmd = static_cast<gpio_num_t>(CONFIG_P64_SD_PIN_CMD);
  slot.d0 = static_cast<gpio_num_t>(CONFIG_P64_SD_PIN_D0);
  slot.d1 = GPIO_NUM_NC;
  slot.d2 = GPIO_NUM_NC;
  slot.d3 = GPIO_NUM_NC;
  slot.width = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;  // never (docs/adr/0006)
  cfg.max_files = 8;
  cfg.allocation_unit_size = 16 * 1024;

  const esp_err_t err = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &cfg, &g_card);
  if (err != ESP_OK) {
    g_card = nullptr;
    if (err == ESP_FAIL) {
      g_error = "no FAT file system on the card";
    } else if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND) {
      g_error = "no card answers";
    } else {
      g_error = esp_err_to_name(err);
    }
    ESP_LOGW(TAG, "mount failed: %s (%s)", g_error.c_str(), esp_err_to_name(err));
    return false;
  }
  g_error.clear();
  uint64_t total = 0, free_bytes = 0;
  esp_vfs_fat_info(kMountPoint, &total, &free_bytes);
  const uint64_t capacity = static_cast<uint64_t>(g_card->csd.capacity) * g_card->csd.sector_size;
  ESP_LOGI(TAG, "mounted at %s: \"%s\" (%s), %llu MB card, FAT %llu MB with %llu MB free, 1-bit bus at %d kHz",
           kMountPoint, g_card->cid.name, g_card->is_mmc ? "MMC" : "SD",
           static_cast<unsigned long long>(capacity >> 20), static_cast<unsigned long long>(total >> 20),
           static_cast<unsigned long long>(free_bytes >> 20), static_cast<int>(g_card->max_freq_khz));
  ensure_dir(root());
  ensure_dir(animations_dir());
  ensure_dir(downloads_dir());
  ensure_dir(cache_dir());
  ensure_dir(channels_dir());
  ensure_dir(state_dir());
  return true;
}

}  // namespace

const char *mount_point() { return kMountPoint; }
std::string root() { return std::string(kMountPoint) + "/" + kRootName; }
std::string animations_dir() { return root() + "/animations"; }
std::string downloads_dir() { return root() + "/downloads"; }
std::string cache_dir() { return root() + "/cache"; }
std::string channels_dir() { return root() + "/channels"; }
std::string state_dir() { return root() + "/state"; }

bool mount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_card) return true;
  return do_mount();
}

bool remount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_card) {
    esp_vfs_fat_sdcard_unmount(kMountPoint, g_card);
    g_card = nullptr;
  }
  return do_mount();
}

bool mounted() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_card != nullptr;
}

CardInfo info() {
  std::lock_guard<std::mutex> lock(g_mutex);
  CardInfo i;
  i.mounted = g_card != nullptr;
  i.error = g_error;
  if (g_card) {
    i.card = g_card->cid.name;
    esp_vfs_fat_info(kMountPoint, &i.total, &i.free);
  }
  return i;
}

bool valid_name(const std::string &name) {
  if (name.empty() || name.size() > kMaxNameChars || name == "." || name == "..") return false;
  for (const char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f) return false;
    if (std::strchr("/\\<>:\"|?*", c) != nullptr) return false;
  }
  return name.front() != ' ' && name.back() != ' ' && name.back() != '.';
}

std::string extension_of(const std::string &name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string::npos || dot == 0) return "";
  std::string ext = name.substr(dot);
  for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

std::vector<FileInfo> list(const std::string &dir) {
  std::vector<FileInfo> out;
  if (!mounted()) return out;
  DIR *d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent *e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.empty() || name.front() == '.') continue;
    struct stat st = {};
    if (stat((dir + "/" + name).c_str(), &st) != 0) continue;
    FileInfo fi;
    fi.name = name;
    fi.directory = S_ISDIR(st.st_mode);
    fi.size = fi.directory ? 0 : static_cast<size_t>(st.st_size);
    out.push_back(std::move(fi));
  }
  closedir(d);
  std::sort(out.begin(), out.end(), [](const FileInfo &a, const FileInfo &b) { return a.name < b.name; });
  return out;
}

bool exists(const std::string &path) {
  struct stat st = {};
  return stat(path.c_str(), &st) == 0;
}

bool read_file(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes, std::string &error) {
  out.clear();
  if (!mounted()) {
    error = "no card mounted";
    return false;
  }
  struct stat st = {};
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    error = "no such file";
    return false;
  }
  if (static_cast<size_t>(st.st_size) > max_bytes) {
    error = "file larger than " + std::to_string(max_bytes) + " bytes";
    return false;
  }
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) {
    error = std::string("cannot open: ") + std::strerror(errno);
    return false;
  }
  out.resize(static_cast<size_t>(st.st_size));
  size_t got = 0;
  while (got < out.size()) {
    const size_t n = std::fread(out.data() + got, 1, std::min<size_t>(64 * 1024, out.size() - got), f);
    if (n == 0) break;
    got += n;
  }
  std::fclose(f);
  if (got != out.size()) {
    error = "short read from the card";
    out.clear();
    return false;
  }
  return true;
}

}  // namespace p64::storage

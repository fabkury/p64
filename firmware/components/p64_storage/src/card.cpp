#include "p64/storage/card.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
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

std::mutex g_mutex;     // mount state
std::mutex g_io_mutex;  // the shared bounce buffer
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

bool resolve(const std::string &relative, std::string &absolute, std::string &error) {
  std::string rel = relative;
  while (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
  while (!rel.empty() && rel.back() == '/') rel.pop_back();
  // Every segment must be a valid name.
  size_t pos = 0;
  while (pos <= rel.size() && !rel.empty()) {
    const size_t slash = rel.find('/', pos);
    const std::string seg = rel.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
    if (!valid_name(seg)) {
      error = "invalid path";
      return false;
    }
    if (slash == std::string::npos) break;
    pos = slash + 1;
  }
  absolute = rel.empty() ? root() : root() + "/" + rel;
  return true;
}

bool is_directory(const std::string &path) {
  struct stat st = {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool remove_path(const std::string &path, std::string &error) {
  if (!mounted()) {
    error = "no card mounted";
    return false;
  }
  struct stat st = {};
  if (stat(path.c_str(), &st) != 0) {
    error = "no such file or folder";
    return false;
  }
  const int rc = S_ISDIR(st.st_mode) ? rmdir(path.c_str()) : unlink(path.c_str());
  if (rc != 0) {
    error = errno == ENOTEMPTY ? "folder not empty" : std::strerror(errno);
    return false;
  }
  return true;
}

bool make_dir(const std::string &path, std::string &error) {
  if (!mounted()) {
    error = "no card mounted";
    return false;
  }
  if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
    error = std::strerror(errno);
    return false;
  }
  return true;
}

bool rename_path(const std::string &from, const std::string &to, std::string &error) {
  if (!mounted()) {
    error = "no card mounted";
    return false;
  }
  if (!exists(from)) {
    error = "no such file or folder";
    return false;
  }
  if (exists(to)) {
    error = "the destination exists";
    return false;
  }
  if (rename(from.c_str(), to.c_str()) != 0) {
    error = std::strerror(errno);
    return false;
  }
  return true;
}

// Card I/O goes through POSIX read()/write() with a bounce buffer in internal DMA-capable
// RAM: newlib's stdio buffer landed in PSRAM once allocations of 4 KB and more went there
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), and the partial-block flush then wrote zeros
// through the SDMMC path (found 2026-09-19: every uploaded file ended in zeros past its
// last full 4 KB block). p3a's loader records the same lesson for reads.
constexpr size_t kIoChunk = 16 * 1024;

uint8_t *io_buffer() {
  static uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(kIoChunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  return buf;
}

bool write_file(const std::string &path, const uint8_t *data, size_t len, std::string &error) {
  if (!mounted()) {
    error = "no card mounted";
    return false;
  }
  uint8_t *buf = io_buffer();
  if (!buf) {
    error = "no internal memory for the write buffer";
    return false;
  }
  const std::string tmp = path + ".tmp";
  const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    error = std::string("cannot create: ") + std::strerror(errno);
    return false;
  }
  size_t written = 0;
  bool ok = true;
  {
    std::lock_guard<std::mutex> lock(g_io_mutex);
    while (ok && written < len) {
      const size_t n = std::min(kIoChunk, len - written);
      std::memcpy(buf, data + written, n);
      const ssize_t w = write(fd, buf, n);
      if (w != static_cast<ssize_t>(n)) ok = false;
      written += n;
    }
  }
  if (ok && fsync(fd) != 0) ok = false;
  close(fd);
  if (!ok) {
    unlink(tmp.c_str());
    error = "write failed (card full or faulty)";
    return false;
  }
  unlink(path.c_str());  // FAT rename does not replace
  if (rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    error = std::string("rename failed: ") + std::strerror(errno);
    return false;
  }
  return true;
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
  uint8_t *buf = io_buffer();
  if (!buf) {
    error = "no internal memory for the read buffer";
    return false;
  }
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    error = std::string("cannot open: ") + std::strerror(errno);
    return false;
  }
  out.resize(static_cast<size_t>(st.st_size));
  size_t got = 0;
  {
    std::lock_guard<std::mutex> lock(g_io_mutex);
    while (got < out.size()) {
      const ssize_t n = read(fd, buf, std::min(kIoChunk, out.size() - got));
      if (n <= 0) break;
      std::memcpy(out.data() + got, buf, static_cast<size_t>(n));
      got += static_cast<size_t>(n);
    }
  }
  close(fd);
  if (got != out.size()) {
    error = "short read from the card";
    out.clear();
    return false;
  }
  return true;
}

}  // namespace p64::storage

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
#include "ff.h"
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
bool g_unformatted = false;  // the mount found a card without a FAT file system
// The card-failed latch (fail_latch.hpp): under g_mutex.
FailLatch g_latch;
void (*g_on_failed)() = nullptr;
constexpr const char *kFailedError = "the card stopped answering";

// An error of the card itself, as opposed to a file that is not there or a full card.
bool io_errno(int e) { return e != ENOENT && e != ENOSPC && e != EEXIST && e != ENOTEMPTY && e != 0; }

void trip() {  // g_mutex not held
  void (*callback)() = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_error = kFailedError;
    callback = g_on_failed;
  }
  ESP_LOGE(TAG, "the card stopped answering: the device goes on without it until the restart");
  if (callback) callback();
}

void note_read(bool ok) {
  bool tripped;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    tripped = ok ? g_latch.read_ok() : g_latch.read_error();
  }
  if (tripped) trip();
}

void note_write_error(bool card_full) {
  bool tripped;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    tripped = g_latch.write_error(card_full);
  }
  if (tripped) trip();
}

void ensure_dir(const std::string &path) {
  struct stat st = {};
  if (stat(path.c_str(), &st) == 0) return;
  if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "cannot create %s: %s", path.c_str(), std::strerror(errno));
  }
}

// Mounts under g_mutex. `format_if_none` formats a card that has no FAT file system
// (format_unformatted(), an explicit user action); otherwise a card is never formatted.
bool do_mount(bool format_if_none = false) {
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
  cfg.format_if_mount_failed = format_if_none;  // never on its own (docs/adr/0006)
  cfg.max_files = 8;
  cfg.allocation_unit_size = 16 * 1024;

  const esp_err_t err = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &cfg, &g_card);
  if (err != ESP_OK) {
    g_card = nullptr;
    g_unformatted = err == ESP_FAIL;
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
  g_unformatted = false;
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

bool format_unformatted(std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_card || !g_unformatted) {
    error = "there is no unformatted card to format";
    return false;
  }
  ESP_LOGW(TAG, "formatting the card (it had no FAT file system)");
#ifdef CONFIG_P64_TEST_HOOKS
  // The virtual eject stands in for an unformatted card: the real one in the slot has its
  // file system, so this mount formats nothing (it formats only when no file system is found).
#endif
  if (!do_mount(true)) {
    error = "format failed: " + g_error;
    return false;
  }
  ESP_LOGI(TAG, "card formatted and mounted; p64 folders created");
  return true;
}

bool format(std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_card || g_latch.tripped()) {
    error = "no card mounted";
    return false;
  }
  ESP_LOGW(TAG, "formatting the card");
  const esp_err_t err = esp_vfs_fat_sdcard_format(kMountPoint, g_card);
  if (err != ESP_OK) {
    error = std::string("format failed: ") + esp_err_to_name(err);
    ESP_LOGE(TAG, "%s", error.c_str());
    return false;
  }
  ensure_dir(root());
  ensure_dir(animations_dir());
  ensure_dir(downloads_dir());
  ensure_dir(cache_dir());
  ensure_dir(channels_dir());
  ensure_dir(state_dir());
  ESP_LOGI(TAG, "card formatted; p64 folders recreated");
  return true;
}

const char *mount_point() { return kMountPoint; }
std::string root() { return std::string(kMountPoint) + "/" + kRootName; }
std::string animations_dir() { return root() + "/animations"; }
std::string downloads_dir() { return root() + "/downloads"; }
std::string cache_dir() { return root() + "/cache"; }
std::string channels_dir() { return root() + "/channels"; }
std::string state_dir() { return root() + "/state"; }

#ifdef CONFIG_P64_TEST_HOOKS
namespace {
bool g_test_ejected = false;
bool g_test_io_fails = false;
}
void test_fail_io() {
  g_test_io_fails = true;
  ESP_LOGW(TAG, "test hook: card I/O fails from now on");
}
void test_eject(const std::string &reason) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_test_ejected = true;
  g_unformatted = reason == "no FAT file system on the card";
  g_error = reason;
  ESP_LOGW(TAG, "test hook: the card is ejected for this boot (%s)", reason.c_str());
}
#endif

bool mount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_card) return true;
#ifdef CONFIG_P64_TEST_HOOKS
  if (g_test_ejected) return false;
#endif
  return do_mount();
}

bool mounted() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_card != nullptr && !g_latch.tripped();
}

void on_failed(void (*callback)()) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_on_failed = callback;
}

CardInfo info() {
  std::lock_guard<std::mutex> lock(g_mutex);
  CardInfo i;
  i.failed = g_latch.tripped();
  i.mounted = g_card != nullptr && !i.failed;
  i.unformatted = g_unformatted && !g_card;
  i.error = g_error;
  if (i.mounted) {
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

namespace {

// The FAT time stamp as a time_t, the way the VFS layer's stat() resolves it (a local
// time; the C library decides the DST flag), so callers see the same mtime either way.
int64_t fat_mtime(const FILINFO &fno) {
  struct tm tm = {};
  tm.tm_mday = fno.fdate & 0x1F;
  tm.tm_mon = ((fno.fdate >> 5) & 0x0F) - 1;
  tm.tm_year = (fno.fdate >> 9) + 80;
  tm.tm_sec = (fno.ftime & 0x1F) * 2;
  tm.tm_min = (fno.ftime >> 5) & 0x3F;
  tm.tm_hour = fno.ftime >> 11;
  tm.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&tm));
}

// The card is the only FAT volume (esp_vfs_fat_sdmmc_mount takes drive 0), so a VFS path
// under the mount point is "0:" plus the rest for FatFs's own API. FatFs is built
// re-entrant (FF_FS_REENTRANT), so these calls may run beside the VFS ones.
std::string fat_path(const std::string &dir) {
  const std::string mp = kMountPoint;
  if (dir.compare(0, mp.size(), mp) != 0) return "";
  std::string rest = dir.substr(mp.size());
  if (rest.empty()) rest = "/";
  return "0:" + rest;
}

// The old way, one stat() per entry: the fallback when FatFs will not open the folder.
std::vector<FileInfo> list_posix(const std::string &dir) {
  std::vector<FileInfo> out;
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
    fi.mtime = static_cast<int64_t>(st.st_mtime);
    out.push_back(std::move(fi));
  }
  closedir(d);
  return out;
}

}  // namespace

std::vector<FileInfo> list(const std::string &dir) {
  std::vector<FileInfo> out;
  if (!mounted()) return out;
  const std::string fpath = fat_path(dir);
  FF_DIR d;
  if (fpath.empty() || f_opendir(&d, fpath.c_str()) != FR_OK) {
    out = list_posix(dir);
  } else {
    FILINFO fno;
    while (f_readdir(&d, &fno) == FR_OK && fno.fname[0] != 0) {
      if (fno.fname[0] == '.') continue;
      FileInfo fi;
      fi.name = fno.fname;
      fi.directory = (fno.fattrib & AM_DIR) != 0;
      fi.size = fi.directory ? 0 : static_cast<size_t>(fno.fsize);
      fi.mtime = fat_mtime(fno);
      out.push_back(std::move(fi));
    }
    f_closedir(&d);
  }
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
#ifdef CONFIG_P64_TEST_HOOKS
  if (g_test_io_fails) {
    note_write_error(false);
    error = "write failed (card full or faulty)";
    return false;
  }
#endif
  const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    const int e = errno;
    error = std::string("cannot create: ") + std::strerror(e);
    if (e == EIO) note_write_error(false);
    return false;
  }
  size_t written = 0;
  bool ok = true;
  int write_errno = 0;
  {
    std::lock_guard<std::mutex> lock(g_io_mutex);
    while (ok && written < len) {
      const size_t n = std::min(kIoChunk, len - written);
      std::memcpy(buf, data + written, n);
      errno = 0;
      const ssize_t w = write(fd, buf, n);
      if (w != static_cast<ssize_t>(n)) {
        ok = false;
        write_errno = errno ? errno : ENOSPC;  // a short write with no error is a full card
      }
      written += n;
    }
  }
  if (ok && fsync(fd) != 0) {
    ok = false;
    write_errno = errno;
  }
  close(fd);
  if (!ok) {
    unlink(tmp.c_str());
    error = "write failed (card full or faulty)";
    note_write_error(!io_errno(write_errno));
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
#ifdef CONFIG_P64_TEST_HOOKS
  if (g_test_io_fails) {
    note_read(false);
    error = "cannot open: I/O error";
    return false;
  }
#endif
  struct stat st = {};
  errno = 0;
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    if (errno == EIO) note_read(false);  // the card, not the file
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
    const int e = errno;
    error = std::string("cannot open: ") + std::strerror(e);
    if (io_errno(e)) note_read(false);
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
    note_read(false);
    return false;
  }
  note_read(true);
  return true;
}

}  // namespace p64::storage

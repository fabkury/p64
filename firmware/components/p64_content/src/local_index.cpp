#include "p64/content/local_index.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace p64::content {
namespace {

bool ends_with_ci(const char *name, const char *ext) {
  const size_t n = std::strlen(name), e = std::strlen(ext);
  if (n < e) return false;
  for (size_t i = 0; i < e; ++i) {
    char a = name[n - e + i], b = ext[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

}  // namespace

bool artwork_extension(const char *name) {
  return ends_with_ci(name, ".gif") || ends_with_ci(name, ".png") || ends_with_ci(name, ".apng") ||
         ends_with_ci(name, ".webp") || ends_with_ci(name, ".bmp");
}

bool scan_folder(const std::string &dir, size_t cap, LocalEntries &out, std::string &error, uint32_t *skipped) {
  out.clear();
  if (skipped) *skipped = 0;
  DIR *d = opendir(dir.c_str());
  if (!d) {
    error = "cannot open " + dir + ": " + std::strerror(errno);
    return false;
  }
  std::string path;
  while (dirent *e = readdir(d)) {
    const char *name = e->d_name;
    if (!name[0] || name[0] == '.') continue;
    if (!artwork_extension(name)) continue;
    const size_t name_len = std::strlen(name);
    if (name_len >= sizeof(LocalEntry::name)) {
      if (skipped) ++*skipped;
      continue;
    }
    path = dir;
    path += '/';
    path += name;
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if (out.size() >= cap) {
      if (skipped) ++*skipped;
      continue;
    }
    LocalEntry entry = {};
    std::memcpy(entry.name, name, name_len + 1);
    entry.mtime = st.st_mtime > 0 ? static_cast<uint32_t>(st.st_mtime) : 0;
    entry.size = static_cast<uint32_t>(st.st_size);
    out.push_back(entry);
  }
  closedir(d);
  std::sort(out.begin(), out.end(), [](const LocalEntry &a, const LocalEntry &b) {
    if (a.mtime != b.mtime) return a.mtime > b.mtime;
    return std::strcmp(a.name, b.name) < 0;
  });
  return true;
}

bool list_subfolders(const std::string &dir, std::vector<std::string> &out, std::string &error) {
  out.clear();
  DIR *d = opendir(dir.c_str());
  if (!d) {
    error = "cannot open " + dir + ": " + std::strerror(errno);
    return false;
  }
  while (dirent *e = readdir(d)) {
    const char *name = e->d_name;
    if (!name[0] || name[0] == '.') continue;
    struct stat st = {};
    if (stat((dir + "/" + name).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    out.emplace_back(name);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return true;
}

}  // namespace p64::content

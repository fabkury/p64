// The card file manager (spec 11.1 Storage): list, upload, delete, make folder, rename.
// Paths are relative to the card root; uploads are validated by sniffing the format.

#include <string>
#include <vector>

#include "esp_log.h"
#include "p64/decode/decoder.hpp"
#include "p64/net/http_server.hpp"
#include "p64/playback/artwork.hpp"
#include "p64/storage/card.hpp"
#include "p64/web/web.hpp"

namespace p64::web::files {
namespace {

constexpr const char *TAG = "files";

esp_err_t list_handler(httpd_req_t *req) {
  std::string rel, abs, error;
  query_param(req, "path", rel);
  if (!storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", error);
  if (!storage::mounted()) return reply_error(req, "503 Service Unavailable", "NO_CARD", "no card mounted");
  if (!storage::is_directory(abs)) return reply_error(req, "404 Not Found", "NOT_FOUND", "no such folder");
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "path", rel.c_str());
  cJSON *arr = cJSON_AddArrayToObject(d, "entries");
  for (const storage::FileInfo &f : storage::list(abs)) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", f.name.c_str());
    cJSON_AddBoolToObject(o, "dir", f.directory);
    cJSON_AddNumberToObject(o, "size", static_cast<double>(f.size));
    cJSON_AddItemToArray(arr, o);
  }
  return reply_ok(req, d);
}

// POST /api/v1/files?path=animations/name.gif with the file as the raw body.
esp_err_t upload_handler(httpd_req_t *req) {
  std::string rel, abs, error;
  if (!query_param(req, "path", rel) || rel.empty()) return reply_error(req, "400 Bad Request", "INVALID_PATH", "path required");
  if (!storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", error);
  if (!storage::mounted()) return reply_error(req, "503 Service Unavailable", "NO_CARD", "no card mounted");
  if (req->content_len == 0 || req->content_len > playback::kMaxFileBytes) {
    return reply_error(req, "413 Payload Too Large", "FILE_TOO_LARGE", "files up to 5 MB");
  }
  std::vector<uint8_t> bytes;
  bytes.resize(req->content_len);
  size_t got = 0;
  while (got < bytes.size()) {
    const int n = httpd_req_recv(req, reinterpret_cast<char *>(bytes.data()) + got, bytes.size() - got);
    if (n <= 0) return reply_error(req, "400 Bad Request", "SHORT_BODY", "upload interrupted");
    got += static_cast<size_t>(n);
  }
  const decode::Format format = decode::sniff(bytes.data(), bytes.size());
  if (format == decode::Format::Unknown) return reply_error(req, "415 Unsupported Media Type", "UNSUPPORTED_TYPE", "not a GIF, PNG, APNG, WebP or BMP");
  // Open it once so a broken file is rejected before it lands on the card.
  {
    std::unique_ptr<decode::Decoder> dec = decode::create(format);
    if (!dec || !dec->open(bytes.data(), bytes.size(), gfx::kBlack)) {
      return reply_error(req, "422 Unprocessable Entity", "REJECTED", std::string("cannot decode: ") + (dec ? dec->error() : ""));
    }
    const decode::Info &info = dec->info();
    if (info.width > playback::kMaxCanvasSide || info.height > playback::kMaxCanvasSide) {
      return reply_error(req, "422 Unprocessable Entity", "REJECTED", "canvas larger than 256x256");
    }
  }
  const size_t slash = abs.rfind('/');
  if (slash != std::string::npos && !storage::is_directory(abs.substr(0, slash))) {
    return reply_error(req, "404 Not Found", "NOT_FOUND", "no such folder");
  }
  if (!storage::write_file(abs, bytes.data(), bytes.size(), error)) return reply_error(req, "500 Internal Server Error", "WRITE_FAILED", error);
  ESP_LOGI(TAG, "uploaded %s (%u bytes, %s)", rel.c_str(), static_cast<unsigned>(bytes.size()), decode::format_name(format));
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "path", rel.c_str());
  cJSON_AddNumberToObject(d, "size", static_cast<double>(bytes.size()));
  cJSON_AddStringToObject(d, "format", decode::format_name(format));
  return reply_ok(req, d);
}

// GET /api/v1/files/get?path=... streams the file (the UI's file preview, and a way to
// check what is on the card byte for byte).
esp_err_t get_handler(httpd_req_t *req) {
  std::string rel, abs, error;
  if (!query_param(req, "path", rel) || rel.empty()) return reply_error(req, "400 Bad Request", "INVALID_PATH", "path required");
  if (!storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", error);
  std::vector<uint8_t> bytes;
  if (!storage::read_file(abs, bytes, playback::kMaxFileBytes, error)) return reply_error(req, "404 Not Found", "NOT_FOUND", error);
  const decode::Format format = decode::sniff(bytes.data(), bytes.size());
  const char *type = "application/octet-stream";
  switch (format) {
    case decode::Format::Gif: type = "image/gif"; break;
    case decode::Format::Png:
    case decode::Format::Apng: type = "image/png"; break;
    case decode::Format::WebP: type = "image/webp"; break;
    case decode::Format::Bmp: type = "image/bmp"; break;
    default: break;
  }
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

esp_err_t delete_handler(httpd_req_t *req) {
  std::string rel, abs, error;
  if (!query_param(req, "path", rel) || rel.empty()) return reply_error(req, "400 Bad Request", "INVALID_PATH", "path required");
  if (!storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", error);
  if (abs == storage::root() || abs == storage::animations_dir() || abs == storage::cache_dir() ||
      abs == storage::channels_dir() || abs == storage::state_dir() || abs == storage::downloads_dir()) {
    return reply_error(req, "403 Forbidden", "PROTECTED", "the p64 folders cannot be deleted");
  }
  if (!storage::remove_path(abs, error)) return reply_error(req, "404 Not Found", "NOT_FOUND", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "path", rel.c_str());
  return reply_ok(req, d);
}

esp_err_t mkdir_handler(httpd_req_t *req) {
  std::string rel, abs, error;
  if (!query_param(req, "path", rel) || rel.empty()) return reply_error(req, "400 Bad Request", "INVALID_PATH", "path required");
  if (!storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", error);
  if (!storage::make_dir(abs, error)) return reply_error(req, "500 Internal Server Error", "MKDIR_FAILED", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "path", rel.c_str());
  return reply_ok(req, d);
}

esp_err_t rename_handler(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *f = cJSON_GetObjectItemCaseSensitive(body, "from");
  const cJSON *t = cJSON_GetObjectItemCaseSensitive(body, "to");
  const std::string from_rel = (f && cJSON_IsString(f) && f->valuestring) ? f->valuestring : "";
  const std::string to_rel = (t && cJSON_IsString(t) && t->valuestring) ? t->valuestring : "";
  cJSON_Delete(body);
  std::string from_abs, to_abs, error;
  if (from_rel.empty() || to_rel.empty() || !storage::resolve(from_rel, from_abs, error) || !storage::resolve(to_rel, to_abs, error)) {
    return reply_error(req, "400 Bad Request", "INVALID_PATH", "from and to required");
  }
  if (!storage::rename_path(from_abs, to_abs, error)) return reply_error(req, "409 Conflict", "RENAME_FAILED", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "from", from_rel.c_str());
  cJSON_AddStringToObject(d, "to", to_rel.c_str());
  return reply_ok(req, d);
}

}  // namespace

void register_routes() {
  const httpd_uri_t routes[] = {
      {"/api/v1/files", HTTP_GET, list_handler, nullptr, false, false, nullptr},
      {"/api/v1/files/get", HTTP_GET, get_handler, nullptr, false, false, nullptr},
      {"/api/v1/files", HTTP_POST, upload_handler, nullptr, false, false, nullptr},
      {"/api/v1/files", HTTP_DELETE, delete_handler, nullptr, false, false, nullptr},
      {"/api/v1/files/mkdir", HTTP_POST, mkdir_handler, nullptr, false, false, nullptr},
      {"/api/v1/files/rename", HTTP_POST, rename_handler, nullptr, false, false, nullptr},
  };
  for (const httpd_uri_t &r : routes) net::http::add(r);
}

}  // namespace p64::web::files

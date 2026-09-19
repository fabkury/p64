// The content routes: playsets (list, read, write, delete, activate), the active
// playset's channels, the history, the local folders, and the show's transport actions.
// Playset files are read and written here (card I/O on the HTTP task, core 0); the show
// is only ever asked to activate one.
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"
#include "p64/content/local_index.hpp"
#include "p64/content/playset.hpp"
#include "p64/content/playset_json.hpp"
#include "p64/content/playset_store.hpp"
#include "p64/net/http_server.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/web/web.hpp"

namespace p64::web::content {
namespace {

constexpr const char *TAG = "web";
constexpr const char *kPrefix = "/api/v1/playsets/";

// The playset name from /api/v1/playsets/<name>; empty when malformed.
std::string name_of(httpd_req_t *req) {
  const char *uri = req->uri;
  if (std::strncmp(uri, kPrefix, std::strlen(kPrefix)) != 0) return "";
  std::string name = uri + std::strlen(kPrefix);
  const size_t end = name.find_first_of("?/");
  if (end != std::string::npos) name.resize(end);
  return p64::content::valid_playset_name(name) ? name : "";
}

esp_err_t list_handler(httpd_req_t *req) {
  if (!hooks().playsets) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no show");
  return reply_ok(req, hooks().playsets());
}

esp_err_t get_handler(httpd_req_t *req) {
  const std::string raw = req->uri + std::strlen(kPrefix);
  p64::content::Builtin b;
  const std::string bare = raw.substr(0, raw.find_first_of("?/"));
  if (p64::content::builtin_from_name(bare, b)) {
    std::vector<std::string> folders{""};
    if (b == p64::content::Builtin::Local && storage::mounted()) {
      std::vector<std::string> subs;
      std::string e;
      if (p64::content::list_subfolders(storage::animations_dir(), subs, e)) folders.insert(folders.end(), subs.begin(), subs.end());
    }
    return reply_ok(req, p64::content::playset_to_json(p64::content::builtin_playset(b, folders)));
  }
  const std::string name = name_of(req);
  if (name.empty()) return reply_error(req, "400 Bad Request", "INVALID_NAME", "playset names are 1 to 32 letters, digits or underscores");
  p64::content::Playset p;
  std::string error;
  if (!p64::content::store::load(name, p, error)) return reply_error(req, "404 Not Found", "NOT_FOUND", error);
  return reply_ok(req, p64::content::playset_to_json(p));
}

esp_err_t put_handler(httpd_req_t *req) {
  const std::string name = name_of(req);
  if (name.empty()) return reply_error(req, "400 Bad Request", "INVALID_NAME", "playset names are 1 to 32 letters, digits or underscores");
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  p64::content::Playset p;
  std::string error;
  const bool parsed = p64::content::playset_from_json(body, p, error);
  cJSON_Delete(body);
  if (!parsed) return reply_error(req, "400 Bad Request", "INVALID_PLAYSET", error);
  p.name = name;
  p.builtin = false;
  if (!p.validate(error)) return reply_error(req, "400 Bad Request", "INVALID_PLAYSET", error);
  if (!storage::mounted()) return reply_error(req, "503 Service Unavailable", "NO_CARD", "playsets are stored on the card");
  if (!p64::content::store::save(p, error)) return reply_error(req, "409 Conflict", "SAVE_FAILED", error);
  system::publish(system::Event::PlaysetsChanged);
  notify();
  return reply_ok(req, p64::content::playset_to_json(p));
}

esp_err_t delete_handler(httpd_req_t *req) {
  const std::string name = name_of(req);
  if (name.empty()) return reply_error(req, "400 Bad Request", "INVALID_NAME", "playset names are 1 to 32 letters, digits or underscores");
  std::string error;
  if (!p64::content::store::remove(name, error)) return reply_error(req, "404 Not Found", "NOT_FOUND", error);
  system::publish(system::Event::PlaysetsChanged);
  notify();
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "name", name.c_str());
  return reply_ok(req, d);
}

esp_err_t channels_handler(httpd_req_t *req) {
  if (!hooks().channels) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no show");
  return reply_ok(req, hooks().channels());
}

esp_err_t history_handler(httpd_req_t *req) {
  if (!hooks().history) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no show");
  return reply_ok(req, hooks().history());
}

// GET /api/v1/folders: the folders that can be local channels, with their file counts.
esp_err_t folders_handler(httpd_req_t *req) {
  if (!storage::mounted()) return reply_error(req, "503 Service Unavailable", "NO_CARD", "no card mounted");
  std::vector<std::string> folders{""};
  std::vector<std::string> subs;
  std::string error;
  if (!p64::content::list_subfolders(storage::animations_dir(), subs, error)) return reply_error(req, "500 Internal Server Error", "READ_FAILED", error);
  folders.insert(folders.end(), subs.begin(), subs.end());
  cJSON *arr = cJSON_CreateArray();
  for (const std::string &f : folders) {
    const std::string dir = f.empty() ? storage::animations_dir() : storage::animations_dir() + "/" + f;
    size_t files = 0;
    for (const storage::FileInfo &fi : storage::list(dir)) {
      if (!fi.directory && p64::content::artwork_extension(fi.name.c_str())) ++files;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "folder", f.c_str());
    cJSON_AddStringToObject(o, "name", f.empty() ? "animations" : f.c_str());
    cJSON_AddNumberToObject(o, "files", static_cast<double>(files));
    cJSON_AddItemToArray(arr, o);
  }
  return reply_ok(req, arr);
}

esp_err_t simple_action(httpd_req_t *req, const char *name, const std::function<void()> &fn) {
  if (!fn) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no show");
  fn();
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "action", name);
  return reply_ok(req, d);
}

esp_err_t action_previous(httpd_req_t *req) { return simple_action(req, "previous", hooks().previous); }
esp_err_t action_pause(httpd_req_t *req) { return simple_action(req, "pause", hooks().pause); }
esp_err_t action_resume(httpd_req_t *req) { return simple_action(req, "resume", hooks().resume); }
esp_err_t action_reset_timer(httpd_req_t *req) { return simple_action(req, "reset_timer", hooks().reset_timer); }
esp_err_t action_refresh(httpd_req_t *req) { return simple_action(req, "refresh", hooks().refresh); }

esp_err_t action_play_playset(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *n = cJSON_GetObjectItemCaseSensitive(body, "name");
  const std::string name = (n && cJSON_IsString(n) && n->valuestring) ? n->valuestring : "";
  cJSON_Delete(body);
  if (name.empty()) return reply_error(req, "400 Bad Request", "INVALID_NAME", "name required");
  if (!hooks().activate_playset) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no show");
  std::string error;
  if (!hooks().activate_playset(name, error)) return reply_error(req, "404 Not Found", "NOT_FOUND", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "name", name.c_str());
  cJSON_AddBoolToObject(d, "activating", true);
  return reply_ok(req, d);
}

esp_err_t action_history_go(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(body, "position");
  const double pos = (p && cJSON_IsNumber(p)) ? p->valuedouble : -1;
  cJSON_Delete(body);
  if (pos < 0 || pos > 1000) return reply_error(req, "400 Bad Request", "INVALID_POSITION", "position required");
  if (!hooks().go_to) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no show");
  hooks().go_to(static_cast<size_t>(pos));
  cJSON *d = cJSON_CreateObject();
  cJSON_AddNumberToObject(d, "position", pos);
  return reply_ok(req, d);
}

}  // namespace

void register_routes() {
  const httpd_uri_t routes[] = {
      {"/api/v1/playsets", HTTP_GET, list_handler, nullptr, false, false, nullptr},
      {"/api/v1/playsets/*", HTTP_GET, get_handler, nullptr, false, false, nullptr},
      {"/api/v1/playsets/*", HTTP_PUT, put_handler, nullptr, false, false, nullptr},
      {"/api/v1/playsets/*", HTTP_DELETE, delete_handler, nullptr, false, false, nullptr},
      {"/api/v1/channels", HTTP_GET, channels_handler, nullptr, false, false, nullptr},
      {"/api/v1/history", HTTP_GET, history_handler, nullptr, false, false, nullptr},
      {"/api/v1/folders", HTTP_GET, folders_handler, nullptr, false, false, nullptr},
      {"/api/v1/action/previous", HTTP_POST, action_previous, nullptr, false, false, nullptr},
      {"/api/v1/action/pause", HTTP_POST, action_pause, nullptr, false, false, nullptr},
      {"/api/v1/action/resume", HTTP_POST, action_resume, nullptr, false, false, nullptr},
      {"/api/v1/action/reset_timer", HTTP_POST, action_reset_timer, nullptr, false, false, nullptr},
      {"/api/v1/action/refresh", HTTP_POST, action_refresh, nullptr, false, false, nullptr},
      {"/api/v1/action/play_playset", HTTP_POST, action_play_playset, nullptr, false, false, nullptr},
      {"/api/v1/action/history_go", HTTP_POST, action_history_go, nullptr, false, false, nullptr},
  };
  for (const httpd_uri_t &r : routes) net::http::add(r);
  ESP_LOGD(TAG, "content routes registered");
}

}  // namespace p64::web::content

// Makapix Club routes: pairing status and control, likes. Play-this of a post or a URL
// goes through POST /api/v1/action/play (api.cpp), which hands it to the same hooks.
#include <string>

#include "cJSON.h"
#include "esp_timer.h"
#include "p64/makapix/makapix.hpp"
#include "p64/system/settings.hpp"
#include "p64/net/http_server.hpp"
#include "p64/web/web.hpp"

namespace p64::web::makapix_routes {
namespace {

const char *state_name(makapix::State s) {
  switch (s) {
    case makapix::State::Unpaired: return "unpaired";
    case makapix::State::Pairing: return "pairing";
    case makapix::State::Paired: return "paired";
    case makapix::State::Invalid: return "invalid";
  }
  return "unknown";
}

cJSON *status_json() {
  const makapix::Status s = makapix::status();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "state", state_name(s.state));
  cJSON_AddStringToObject(o, "host", s.host.c_str());
  cJSON_AddStringToObject(o, "player_key", s.player_key.c_str());
  cJSON_AddStringToObject(o, "code", s.code.c_str());
  const int64_t left = s.code_expires_us ? (s.code_expires_us - esp_timer_get_time()) / 1000000 : 0;
  cJSON_AddNumberToObject(o, "code_seconds_left", left > 0 ? static_cast<double>(left) : 0);
  cJSON_AddBoolToObject(o, "online", s.online);
  cJSON_AddBoolToObject(o, "mqtt_connected", s.mqtt_connected);
  cJSON_AddStringToObject(o, "activity", s.activity.c_str());
  cJSON_AddStringToObject(o, "last_error", s.last_error.c_str());
  cJSON_AddNumberToObject(o, "cert_expires_at", s.cert_expires_at);
  cJSON_AddNumberToObject(o, "refreshes", s.refreshes);
  cJSON_AddNumberToObject(o, "downloads", s.downloads);
  cJSON_AddNumberToObject(o, "download_failures", s.download_failures);
  cJSON_AddNumberToObject(o, "views_sent", s.views_sent);
  cJSON_AddNumberToObject(o, "commands", s.commands);
  cJSON *c = cJSON_AddObjectToObject(o, "cache");
  cJSON_AddNumberToObject(c, "files", s.cache_files);
  cJSON_AddNumberToObject(c, "bytes", static_cast<double>(s.cache_bytes));
  cJSON_AddNumberToObject(c, "last_sweep", s.last_sweep);
  cJSON_AddNumberToObject(c, "last_deleted", s.last_sweep_deleted);
  cJSON_AddNumberToObject(c, "last_freed_bytes", static_cast<double>(s.last_sweep_freed));
  return o;
}

// The cache sweep now (spec 5.4), for tests and for "what would go": older_than_s
// defaults to the cache retention, dry_run to true.
esp_err_t sweep_handler(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *age = cJSON_GetObjectItemCaseSensitive(body, "older_than_s");
  const cJSON *dry = cJSON_GetObjectItemCaseSensitive(body, "dry_run");
  const double age_s = (age && cJSON_IsNumber(age)) ? age->valuedouble : static_cast<double>(system::settings().cache_retention_days) * 86400.0;
  const bool dry_run = dry ? cJSON_IsTrue(dry) : true;
  cJSON_Delete(body);
  if (age_s < 0 || age_s > 4294967295.0) return reply_error(req, "400 Bad Request", "INVALID_ARG", "older_than_s out of range");
  makapix::SweepResult r;
  std::string error;
  if (!makapix::cache_sweep(static_cast<uint32_t>(age_s), dry_run, r, error)) return reply_error(req, "409 Conflict", "SWEEP_UNAVAILABLE", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "dry_run", r.dry_run);
  cJSON_AddNumberToObject(d, "older_than_s", r.older_than_s);
  cJSON_AddNumberToObject(d, "examined", r.examined);
  cJSON_AddNumberToObject(d, "bytes", static_cast<double>(r.bytes));
  cJSON_AddNumberToObject(d, "deleted", r.deleted);
  cJSON_AddNumberToObject(d, "freed_bytes", static_cast<double>(r.freed));
  cJSON_AddNumberToObject(d, "indexes_deleted", r.indexes_deleted);
  cJSON_AddNumberToObject(d, "downloads_deleted", r.downloads_deleted);
  cJSON_AddNumberToObject(d, "took_ms", r.took_ms);
  return reply_ok(req, d);
}

esp_err_t get_handler(httpd_req_t *req) { return reply_ok(req, status_json()); }

esp_err_t pair_handler(httpd_req_t *req) {
  std::string error;
  if (!makapix::pair(error)) return reply_error(req, "409 Conflict", "PAIR_FAILED", error);
  return reply_ok(req, status_json());
}

esp_err_t cancel_handler(httpd_req_t *req) {
  makapix::cancel_pairing();
  return reply_ok(req, status_json());
}

esp_err_t unpair_handler(httpd_req_t *req) {
  std::string error;
  if (!makapix::unpair(error)) return reply_error(req, "500 Internal Server Error", "UNPAIR_FAILED", error);
  return reply_ok(req, status_json());
}

esp_err_t like_handler(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(body, "post_id");
  const cJSON *liked = cJSON_GetObjectItemCaseSensitive(body, "like");
  const int32_t post_id = (id && cJSON_IsNumber(id)) ? static_cast<int32_t>(id->valuedouble) : -1;
  const bool want = !liked || !cJSON_IsFalse(liked);
  cJSON_Delete(body);
  if (post_id < 0) return reply_error(req, "400 Bad Request", "INVALID_POST", "post_id required");
  if (!hooks().like) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no Makapix");
  std::string error;
  if (!hooks().like(post_id, want, error)) return reply_error(req, "409 Conflict", "LIKE_FAILED", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddNumberToObject(d, "post_id", post_id);
  cJSON_AddBoolToObject(d, "liked", want);
  return reply_ok(req, d);
}

}  // namespace

cJSON *makapix_status() { return status_json(); }

void register_routes() {
  const httpd_uri_t routes[] = {
      {"/api/v1/makapix", HTTP_GET, get_handler, nullptr, false, false, nullptr},
      {"/api/v1/makapix/pair", HTTP_POST, pair_handler, nullptr, false, false, nullptr},
      {"/api/v1/makapix/pair/cancel", HTTP_POST, cancel_handler, nullptr, false, false, nullptr},
      {"/api/v1/makapix/unpair", HTTP_POST, unpair_handler, nullptr, false, false, nullptr},
      {"/api/v1/makapix/like", HTTP_POST, like_handler, nullptr, false, false, nullptr},
      {"/api/v1/diag/cache_sweep", HTTP_POST, sweep_handler, nullptr, false, false, nullptr},
  };
  for (const httpd_uri_t &r : routes) net::http::add(r);
}

}  // namespace p64::web::makapix_routes

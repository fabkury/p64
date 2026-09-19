#include <cinttypes>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p64/decode/decoder.hpp"
#include "p64/gfx/png_encode.hpp"
#include "p64/net/clock.hpp"
#include "p64/net/http_server.hpp"
#include "p64/net/tz.hpp"
#include "p64/net/wifi.hpp"
#include "p64/playback/artwork.hpp"
#include "p64/storage/card.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/log_ring.hpp"
#include "p64/system/reliability.hpp"
#include "p64/system/settings.hpp"
#include "p64/web/web.hpp"
#include "p64/inputs/inputs.hpp"
#include "p64/ota/ota.hpp"
#include "p64/stream/stream.hpp"
#include "p64/widgets/widgets.hpp"

namespace p64::web {

constexpr int kApiVersion = 1;
Hooks g_hooks;

namespace files {
void register_routes();
}
namespace content {
void register_routes();
}  // namespace content
namespace makapix_routes {
void register_routes();
cJSON *makapix_status();
}  // namespace makapix_routes
namespace ws {
void register_routes();
void start();
void notify();
}  // namespace ws
namespace auth {
void register_routes();
bool gate(httpd_req_t *req);
}  // namespace auth

namespace {

constexpr const char *TAG = "api";

std::string url_decode(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size()) {
      out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

const char *mode_name(display::Mode m) { return m == display::Mode::Photo ? "photo" : "quality"; }

}  // namespace

// --- helpers ------------------------------------------------------------------

esp_err_t reply_ok(httpd_req_t *req, cJSON *data) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  if (data) {
    cJSON_AddItemToObject(root, "data", data);
  } else {
    cJSON_AddNullToObject(root, "data");
  }
  char *text = cJSON_PrintUnformatted(root);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t r = net::http::send(req, "200 OK", "application/json", text ? text : "{\"ok\":true}");
  cJSON_free(text);
  cJSON_Delete(root);
  return r;
}

esp_err_t reply_error(httpd_req_t *req, const char *status, const char *code, const std::string &message) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", message.c_str());
  cJSON_AddStringToObject(root, "code", code);
  char *text = cJSON_PrintUnformatted(root);
  const esp_err_t r = net::http::send(req, status, "application/json", text ? text : "{\"ok\":false}");
  cJSON_free(text);
  cJSON_Delete(root);
  return r;
}

bool query_param(httpd_req_t *req, const char *name, std::string &out) {
  const size_t len = httpd_req_get_url_query_len(req);
  if (len == 0) return false;
  std::string query(len + 1, '\0');
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return false;
  std::string value(1024, '\0');
  if (httpd_query_key_value(query.c_str(), name, value.data(), value.size()) != ESP_OK) return false;
  value.resize(std::strlen(value.c_str()));
  out = url_decode(value);
  return true;
}

cJSON *parse_body(httpd_req_t *req) {
  std::string body;
  if (!net::http::read_body(req, body, 32 * 1024)) {
    reply_error(req, "413 Payload Too Large", "PAYLOAD_TOO_LARGE", "body over 32 KB");
    return nullptr;
  }
  cJSON *json = cJSON_Parse(body.c_str());
  if (!json) {
    reply_error(req, "400 Bad Request", "INVALID_JSON", "the body is not valid JSON");
    return nullptr;
  }
  return json;
}

// --- status -------------------------------------------------------------------

cJSON *build_status() {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddNumberToObject(d, "api_version", kApiVersion);
  const esp_app_desc_t *app = esp_app_get_description();
  cJSON *fw = cJSON_AddObjectToObject(d, "firmware");
  cJSON_AddStringToObject(fw, "version", app->version);
  cJSON_AddStringToObject(fw, "idf", app->idf_ver);
  cJSON_AddStringToObject(fw, "built", (std::string(app->date) + " " + app->time).c_str());
  cJSON_AddNumberToObject(d, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000));

  cJSON *heap = cJSON_AddObjectToObject(d, "heap");
  cJSON_AddNumberToObject(heap, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(heap, "internal_largest",
                          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(heap, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  const net::wifi::Status w = net::wifi::status();
  cJSON *net = cJSON_AddObjectToObject(d, "network");
  cJSON_AddBoolToObject(net, "connected", w.connected);
  cJSON_AddBoolToObject(net, "setup_mode", w.setup_mode);
  cJSON_AddStringToObject(net, "ssid", w.ssid.c_str());
  cJSON_AddStringToObject(net, "ip", w.ip.c_str());
  cJSON_AddStringToObject(net, "gateway", w.gateway.c_str());
  cJSON_AddStringToObject(net, "netmask", w.netmask.c_str());
  cJSON_AddNumberToObject(net, "rssi", w.rssi);
  cJSON_AddStringToObject(net, "hostname", w.hostname.c_str());

  cJSON *tm = cJSON_AddObjectToObject(d, "time");
  struct tm t;
  const bool synced = net::clock::local_time(t);
  cJSON_AddBoolToObject(tm, "synced", synced);
  cJSON_AddStringToObject(tm, "source", net::clock::source());
  if (synced) {
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t);
    cJSON_AddStringToObject(tm, "local", buf);
  }
  cJSON_AddStringToObject(tm, "rule", net::clock::posix_rule().c_str());

  const storage::CardInfo c = storage::info();
  cJSON *card = cJSON_AddObjectToObject(d, "card");
  cJSON_AddBoolToObject(card, "mounted", c.mounted);
  cJSON_AddStringToObject(card, "name", c.card.c_str());
  cJSON_AddNumberToObject(card, "total_bytes", static_cast<double>(c.total));
  cJSON_AddNumberToObject(card, "free_bytes", static_cast<double>(c.free));
  cJSON_AddStringToObject(card, "error", c.error.c_str());

  if (g_hooks.display && g_hooks.display()) {
    const display::Display::Health h = g_hooks.display()->health();
    cJSON *panel = cJSON_AddObjectToObject(d, "panel");
    cJSON_AddStringToObject(panel, "mode", mode_name(h.mode));
    cJSON_AddNumberToObject(panel, "refresh_hz", h.refresh_hz);
    cJSON_AddNumberToObject(panel, "bit_depth", h.bit_depth);
    cJSON_AddNumberToObject(panel, "transition_bit", h.transition_bit);
    cJSON_AddNumberToObject(panel, "dma_priority", h.dma_priority);
    cJSON_AddNumberToObject(panel, "frames", h.frames);
    cJSON_AddNumberToObject(panel, "late_flips", h.late_flips);
    cJSON_AddNumberToObject(panel, "timeouts", h.timeouts);
    cJSON_AddBoolToObject(panel, "stalled", h.stalled);
    cJSON_AddNumberToObject(panel, "restarts", h.restarts);
    cJSON_AddBoolToObject(panel, "dma_sync", h.dma_sync);
    cJSON_AddBoolToObject(panel, "dma_moving", h.dma_moving);
    cJSON_AddNumberToObject(panel, "brightness", g_hooks.display()->brightness());
    cJSON_AddNumberToObject(panel, "rotation", static_cast<int>(g_hooks.display()->rotation()));
    cJSON_AddBoolToObject(panel, "night_active", g_hooks.night_active ? g_hooks.night_active() : false);
  }
  if (g_hooks.playback_status) cJSON_AddItemToObject(d, "playback", g_hooks.playback_status());
  cJSON_AddItemToObject(d, "makapix", makapix_routes::makapix_status());
  {
    const widgets::Reading r = widgets::sensor();
    cJSON *sn = cJSON_AddObjectToObject(d, "sensor");
    cJSON_AddBoolToObject(sn, "valid", r.valid);
    cJSON_AddNumberToObject(sn, "temperature_c", r.temperature_c);
    cJSON_AddNumberToObject(sn, "humidity", r.humidity);
    cJSON_AddNumberToObject(sn, "trend_c_per_hour", r.trend_c_per_hour);
    cJSON_AddItemToObject(d, "weather", widgets::weather_json());
  }
  cJSON_AddItemToObject(d, "stream", stream::status_json());
  cJSON_AddItemToObject(d, "reliability", system::reliability::json());
  cJSON_AddStringToObject(d, "update_state", ota::state_name(ota::status().state));
  {
    cJSON *in = cJSON_AddObjectToObject(d, "inputs");
    cJSON_AddBoolToObject(in, "imu_present", inputs::imu_present());
    cJSON_AddBoolToObject(in, "auto_rotation_resolved", inputs::auto_rotation_resolved());
    cJSON_AddNumberToObject(in, "auto_rotation", inputs::auto_rotation());
    cJSON_AddBoolToObject(in, "calibrated", inputs::calibrated());
  }
  return d;
}

namespace {

esp_err_t status_handler(httpd_req_t *req) { return reply_ok(req, build_status()); }

esp_err_t settings_get(httpd_req_t *req) {
  const std::string json = system::settings().to_json();
  cJSON *data = cJSON_Parse(json.c_str());
  return reply_ok(req, data);
}

esp_err_t settings_put(httpd_req_t *req) {
  std::string body;
  if (!net::http::read_body(req, body, 32 * 1024)) return reply_error(req, "413 Payload Too Large", "PAYLOAD_TOO_LARGE", "body over 32 KB");
  std::string error;
  const system::Settings before = system::settings();
  bool parsed = true;
  const bool saved = system::settings_update([&](system::Settings &s) { parsed = s.apply_json(body.c_str(), error); });
  if (!parsed) return reply_error(req, "400 Bad Request", "INVALID_JSON", error);
  if (!saved) return reply_error(req, "500 Internal Server Error", "SAVE_FAILED", "settings could not be stored");
  const system::Settings after = system::settings();
  if (after.panel_mode != before.panel_mode && g_hooks.request_mode) {
    g_hooks.request_mode(after.panel_mode == system::PanelMode::Photo ? display::Mode::Photo : display::Mode::Quality);
  }
  ws::notify();
  return reply_ok(req, cJSON_Parse(after.to_json().c_str()));
}

esp_err_t action_next(httpd_req_t *req) {
  if (g_hooks.next) g_hooks.next();
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "action", "next");
  return reply_ok(req, d);
}

esp_err_t action_play(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(body, "path");
  const cJSON *post = cJSON_GetObjectItemCaseSensitive(body, "post");
  const cJSON *url = cJSON_GetObjectItemCaseSensitive(body, "url");
  std::string rel = (p && cJSON_IsString(p) && p->valuestring) ? p->valuestring : "";
  const std::string post_ref = (post && cJSON_IsString(post) && post->valuestring) ? post->valuestring : "";
  const std::string url_ref = (url && cJSON_IsString(url) && url->valuestring) ? url->valuestring : "";
  cJSON_Delete(body);
  std::string abs, error;
  if (!post_ref.empty()) {
    if (!g_hooks.play_post) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no Makapix");
    if (!g_hooks.play_post(post_ref, error)) return reply_error(req, "422 Unprocessable Entity", "REJECTED", error);
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "post", post_ref.c_str());
    cJSON_AddBoolToObject(d, "queued", true);
    return reply_ok(req, d);
  }
  if (!url_ref.empty()) {
    if (!g_hooks.play_url) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no downloads");
    if (!g_hooks.play_url(url_ref, error)) return reply_error(req, "422 Unprocessable Entity", "REJECTED", error);
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "url", url_ref.c_str());
    cJSON_AddBoolToObject(d, "queued", true);
    return reply_ok(req, d);
  }
  if (rel.empty() || !storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", "path, post or url required");
  if (!g_hooks.play_file) return reply_error(req, "501 Not Implemented", "NOT_SUPPORTED", "no player");
  if (!g_hooks.play_file(abs, error)) return reply_error(req, "422 Unprocessable Entity", "REJECTED", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "path", rel.c_str());
  return reply_ok(req, d);
}

void reboot_task(void *) {
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
}

esp_err_t action_reboot(httpd_req_t *req) {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "rebooting", true);
  const esp_err_t r = reply_ok(req, d);
  xTaskCreate(reboot_task, "reboot", 2048, nullptr, 5, nullptr);
  return r;
}

// Factory reset (spec 15.4): the body must carry {"confirm": "ERASE"}; answers, then
// erases and reboots from a helper task so the response leaves first.
void factory_reset_task(void *) {
  vTaskDelay(pdMS_TO_TICKS(300));
  if (g_hooks.factory_reset) g_hooks.factory_reset();
  vTaskDelete(nullptr);
}

esp_err_t action_factory_reset(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *c = cJSON_GetObjectItemCaseSensitive(body, "confirm");
  const bool confirmed = c && cJSON_IsString(c) && std::strcmp(c->valuestring, "ERASE") == 0;
  cJSON_Delete(body);
  if (!confirmed) return reply_error(req, "400 Bad Request", "NOT_CONFIRMED", "send {\"confirm\":\"ERASE\"}");
  if (!g_hooks.factory_reset) return reply_error(req, "501 Not Implemented", "UNSUPPORTED", "no factory reset hook");
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "erasing", true);
  const esp_err_t r = reply_ok(req, d);
  xTaskCreate(factory_reset_task, "factory", 4096, nullptr, 5, nullptr);
  return r;
}

// "Set time from this browser" (spec 10.2): {"utc": <seconds since 1970>}.
esp_err_t action_set_time(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *u = cJSON_GetObjectItemCaseSensitive(body, "utc");
  const double utc = (u && cJSON_IsNumber(u)) ? u->valuedouble : 0;
  cJSON_Delete(body);
  if (utc < 1700000000.0 || utc > 4102444800.0) return reply_error(req, "400 Bad Request", "INVALID_ARG", "utc: seconds since 1970, 2023..2100");
  net::clock::set_manual(static_cast<time_t>(utc));
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "synced", net::clock::synced());
  cJSON_AddStringToObject(d, "source", net::clock::source());
  return reply_ok(req, d);
}

esp_err_t diag_imu(httpd_req_t *req) { return reply_ok(req, inputs::imu_json()); }

// "The panel is upright now": {"rotation": 0|90|180|270} (default: the current display
// rotation) becomes the reference for auto-rotation.
esp_err_t action_calibrate_upright(httpd_req_t *req) {
  int rotation = g_hooks.display && g_hooks.display() ? static_cast<int>(g_hooks.display()->rotation()) : 0;
  if (req->content_len > 0) {
    cJSON *body = parse_body(req);
    if (!body) return ESP_OK;
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(body, "rotation");
    if (r && cJSON_IsNumber(r)) rotation = r->valueint;
    cJSON_Delete(body);
  }
  if (!gfx::valid_rotation(rotation)) return reply_error(req, "400 Bad Request", "INVALID_ARG", "rotation 0, 90, 180 or 270");
  if (!inputs::calibrate_upright(static_cast<uint16_t>(rotation))) return reply_error(req, "503 Service Unavailable", "NO_IMU", "no IMU reading yet");
  return reply_ok(req, inputs::imu_json());
}

// Updates (spec 15.2).
esp_err_t update_get(httpd_req_t *req) { return reply_ok(req, ota::status_json()); }

esp_err_t update_check(httpd_req_t *req) {
  if (!ota::check_now()) return reply_error(req, "409 Conflict", "BUSY", "a check or install is running");
  return reply_ok(req, ota::status_json());
}

// {"url": "...", "sha256": "<64 hex>"} installs from any URL; an empty body installs the
// available release (its published checksum is verified).
esp_err_t update_install(httpd_req_t *req) {
  std::string url, sha;
  if (req->content_len > 0) {
    cJSON *body = parse_body(req);
    if (!body) return ESP_OK;
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(body, "url");
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(body, "sha256");
    if (u && cJSON_IsString(u)) url = u->valuestring;
    if (s && cJSON_IsString(s)) sha = s->valuestring;
    cJSON_Delete(body);
  }
  if (!url.empty() && sha.size() != 64) return reply_error(req, "400 Bad Request", "INVALID_ARG", "an install from a URL needs its sha256 (64 hex digits)");
  if (!ota::install(url, sha)) return reply_error(req, "409 Conflict", "BUSY", "nothing available to install, or a job is running");
  return reply_ok(req, ota::status_json());
}

esp_err_t update_rollback(httpd_req_t *req) {
  std::string error;
  if (!ota::rollback(error)) return reply_error(req, "409 Conflict", "NO_ROLLBACK", error);
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "rebooting", true);
  const esp_err_t r = reply_ok(req, d);
  xTaskCreate(reboot_task, "reboot", 2048, nullptr, 5, nullptr);
  return r;
}

esp_err_t diag_coredump_erase(httpd_req_t *req) {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "erased", system::reliability::erase_coredump());
  return reply_ok(req, d);
}

esp_err_t frame_png(httpd_req_t *req) {
  auto frame = std::make_unique<gfx::Frame>();
  if (!g_hooks.snapshot || !g_hooks.snapshot(*frame)) return reply_error(req, "503 Service Unavailable", "NO_FRAME", "nothing presented yet");
  const std::vector<uint8_t> png = gfx::encode_png_rgb(frame->data(), gfx::Frame::width(), gfx::Frame::height());
  httpd_resp_set_type(req, "image/png");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, reinterpret_cast<const char *>(png.data()), png.size());
}

esp_err_t frame_raw(httpd_req_t *req) {
  auto frame = std::make_unique<gfx::Frame>();
  if (!g_hooks.snapshot || !g_hooks.snapshot(*frame)) return reply_error(req, "503 Service Unavailable", "NO_FRAME", "nothing presented yet");
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, reinterpret_cast<const char *>(frame->data()), gfx::Frame::bytes());
}

esp_err_t wifi_scan(httpd_req_t *req) {
  cJSON *arr = cJSON_CreateArray();
  for (const net::wifi::ScanEntry &e : net::wifi::scan()) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", e.ssid.c_str());
    cJSON_AddNumberToObject(o, "rssi", e.rssi);
    cJSON_AddBoolToObject(o, "secure", e.secure);
    cJSON_AddItemToArray(arr, o);
  }
  return reply_ok(req, arr);
}

struct WifiChange {
  bool erase;
  std::string ssid, password;
};

void wifi_change_task(void *arg) {
  auto *c = static_cast<WifiChange *>(arg);
  vTaskDelay(pdMS_TO_TICKS(500));
  if (c->erase) {
    net::wifi::erase_credentials();
  } else {
    net::wifi::save_credentials(c->ssid, c->password);
  }
  delete c;
  vTaskDelete(nullptr);
}

esp_err_t wifi_set(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *s = cJSON_GetObjectItemCaseSensitive(body, "ssid");
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(body, "password");
  const std::string ssid = (s && cJSON_IsString(s) && s->valuestring) ? s->valuestring : "";
  const std::string password = (p && cJSON_IsString(p) && p->valuestring) ? p->valuestring : "";
  cJSON_Delete(body);
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return reply_error(req, "400 Bad Request", "INVALID_ARG", "ssid (1-32) and password (0-64) required");
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "ssid", ssid.c_str());
  cJSON_AddBoolToObject(d, "connecting", true);
  const esp_err_t r = reply_ok(req, d);
  xTaskCreatePinnedToCore(wifi_change_task, "wifi_change", 4096, new WifiChange{false, ssid, password}, 5, nullptr, 0);
  return r;
}

esp_err_t wifi_erase(httpd_req_t *req) {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddBoolToObject(d, "erasing", true);
  const esp_err_t r = reply_ok(req, d);
  xTaskCreatePinnedToCore(wifi_change_task, "wifi_change", 4096, new WifiChange{true, "", ""}, 5, nullptr, 0);
  return r;
}

esp_err_t timezones(httpd_req_t *req) {
  // Streamed as a plain JSON array: 519 names would be a large cJSON tree.
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
  std::string chunk = "[";
  for (size_t i = 0; i < net::tz::count(); ++i) {
    chunk += (i ? ",\"" : "\"");
    chunk += net::tz::name_at(i);
    chunk += "\"";
    if (chunk.size() > 3000) {
      httpd_resp_send_chunk(req, chunk.c_str(), chunk.size());
      chunk.clear();
    }
  }
  chunk += "]";
  httpd_resp_send_chunk(req, chunk.c_str(), chunk.size());
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t diag_log(httpd_req_t *req) {
  std::string bytes_s;
  size_t bytes = 16 * 1024;
  if (query_param(req, "bytes", bytes_s)) bytes = static_cast<size_t>(std::strtoul(bytes_s.c_str(), nullptr, 10));
  const std::string text = system::logring::tail(std::min<size_t>(bytes, 64 * 1024));
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, text.c_str(), text.size());
}

esp_err_t diag_dma(httpd_req_t *req) {
  cJSON *body = parse_body(req);
  if (!body) return ESP_OK;
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(body, "priority");
  const int priority = (p && cJSON_IsNumber(p)) ? p->valueint : -1;
  cJSON_Delete(body);
  if (priority < 0 || priority > 5 || !g_hooks.display || !g_hooks.display()) return reply_error(req, "400 Bad Request", "INVALID_ARG", "priority 0..5");
  if (!g_hooks.display()->set_dma_priority(priority)) return reply_error(req, "500 Internal Server Error", "FAILED", "driver refused");
  cJSON *d = cJSON_CreateObject();
  cJSON_AddNumberToObject(d, "priority", priority);
  return reply_ok(req, d);
}

// Memory: every heap region and every task's stack headroom (needs the FreeRTOS trace
// facility, on in sdkconfig.defaults).
esp_err_t diag_memory(httpd_req_t *req) {
  cJSON *d = cJSON_CreateObject();
  cJSON *heaps = cJSON_AddObjectToObject(d, "heap");
  const struct {
    const char *name;
    uint32_t caps;
  } regions[] = {{"internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT}, {"dma", MALLOC_CAP_DMA}, {"psram", MALLOC_CAP_SPIRAM}};
  for (const auto &r : regions) {
    multi_heap_info_t info;
    heap_caps_get_info(&info, r.caps);
    cJSON *o = cJSON_AddObjectToObject(heaps, r.name);
    cJSON_AddNumberToObject(o, "free", info.total_free_bytes);
    cJSON_AddNumberToObject(o, "allocated", info.total_allocated_bytes);
    cJSON_AddNumberToObject(o, "largest_free", info.largest_free_block);
    cJSON_AddNumberToObject(o, "minimum_free", info.minimum_free_bytes);
  }
  cJSON *tasks = cJSON_AddArrayToObject(d, "tasks");
#if configUSE_TRACE_FACILITY
  const UBaseType_t count = uxTaskGetNumberOfTasks();
  std::vector<TaskStatus_t> status(count + 4);
  const UBaseType_t got = uxTaskGetSystemState(status.data(), status.size(), nullptr);
  for (UBaseType_t i = 0; i < got; ++i) {
    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "name", status[i].pcTaskName);
    cJSON_AddNumberToObject(t, "priority", status[i].uxCurrentPriority);
    cJSON_AddNumberToObject(t, "stack_free", status[i].usStackHighWaterMark);
    cJSON_AddItemToArray(tasks, t);
  }
#endif
  return reply_ok(req, d);
}

// The decode benchmark (spec 4.4): decodes and scales every frame of a file for up to
// `loops` loops or 600 frames and reports the per-frame time. Runs on the HTTP task.
esp_err_t diag_bench(httpd_req_t *req) {
  std::string rel, loops_s;
  if (!query_param(req, "path", rel)) return reply_error(req, "400 Bad Request", "INVALID_ARG", "path required");
  int loops = 2;
  if (query_param(req, "loops", loops_s)) loops = std::max(1, std::min(10, std::atoi(loops_s.c_str())));
  std::string abs, error;
  if (!storage::resolve(rel, abs, error)) return reply_error(req, "400 Bad Request", "INVALID_PATH", error);
  std::vector<uint8_t> bytes;
  if (!storage::read_file(abs, bytes, playback::kMaxFileBytes, error)) return reply_error(req, "404 Not Found", "NOT_FOUND", error);
  auto art = std::make_unique<playback::Artwork>();
  if (!art->open(std::move(bytes), rel, gfx::kBlack, error)) return reply_error(req, "422 Unprocessable Entity", "REJECTED", error);
  auto frame = std::make_unique<gfx::Frame>();
  uint32_t frames = 0, delay_total = 0;
  int64_t total_us = 0, max_us = 0, first_us = 0;
  int loops_done = 0;
  while (loops_done < loops && frames < 600) {
    uint32_t delay = 0;
    const int64_t t0 = esp_timer_get_time();
    if (!art->next_frame(*frame, delay)) break;
    const int64_t dt = esp_timer_get_time() - t0;
    if (frames == 0) first_us = dt;
    total_us += dt;
    max_us = std::max(max_us, dt);
    delay_total += delay;
    ++frames;
    if (art->at_end() || art->is_static()) {
      ++loops_done;
      if (art->is_static()) break;
    }
  }
  cJSON *d = cJSON_CreateObject();
  cJSON_AddStringToObject(d, "path", rel.c_str());
  cJSON_AddStringToObject(d, "format", decode::format_name(art->format()));
  cJSON_AddNumberToObject(d, "width", art->info().width);
  cJSON_AddNumberToObject(d, "height", art->info().height);
  cJSON_AddNumberToObject(d, "file_bytes", static_cast<double>(art->file_bytes()));
  cJSON_AddNumberToObject(d, "frames", frames);
  cJSON_AddNumberToObject(d, "loops", loops_done);
  cJSON_AddNumberToObject(d, "first_frame_ms", first_us / 1000.0);
  cJSON_AddNumberToObject(d, "avg_frame_ms", frames ? total_us / 1000.0 / frames : 0);
  cJSON_AddNumberToObject(d, "max_frame_ms", max_us / 1000.0);
  cJSON_AddNumberToObject(d, "avg_delay_ms", frames ? static_cast<double>(delay_total) / frames : 0);
  cJSON_AddNumberToObject(d, "sustainable_fps", total_us > 0 ? frames * 1e6 / total_us : 0);
  return reply_ok(req, d);
}

}  // namespace

const Hooks &hooks() { return g_hooks; }

void init(const Hooks &hooks) {
  g_hooks = hooks;
  const httpd_uri_t routes[] = {
      {"/api/v1/status", HTTP_GET, status_handler, nullptr, false, false, nullptr},
      {"/api/v1/settings", HTTP_GET, settings_get, nullptr, false, false, nullptr},
      {"/api/v1/settings", HTTP_PUT, settings_put, nullptr, false, false, nullptr},
      {"/api/v1/action/next", HTTP_POST, action_next, nullptr, false, false, nullptr},
      {"/api/v1/action/play", HTTP_POST, action_play, nullptr, false, false, nullptr},
      {"/api/v1/action/reboot", HTTP_POST, action_reboot, nullptr, false, false, nullptr},
      {"/api/v1/action/factory_reset", HTTP_POST, action_factory_reset, nullptr, false, false, nullptr},
      {"/api/v1/action/set_time", HTTP_POST, action_set_time, nullptr, false, false, nullptr},
      {"/api/v1/diag/coredump/erase", HTTP_POST, diag_coredump_erase, nullptr, false, false, nullptr},
      {"/api/v1/diag/imu", HTTP_GET, diag_imu, nullptr, false, false, nullptr},
      {"/api/v1/update", HTTP_GET, update_get, nullptr, false, false, nullptr},
      {"/api/v1/update/check", HTTP_POST, update_check, nullptr, false, false, nullptr},
      {"/api/v1/update/install", HTTP_POST, update_install, nullptr, false, false, nullptr},
      {"/api/v1/update/rollback", HTTP_POST, update_rollback, nullptr, false, false, nullptr},
      {"/api/v1/action/calibrate_upright", HTTP_POST, action_calibrate_upright, nullptr, false, false, nullptr},
      {"/api/v1/frame", HTTP_GET, frame_png, nullptr, false, false, nullptr},
      {"/api/v1/frame.raw", HTTP_GET, frame_raw, nullptr, false, false, nullptr},
      {"/api/v1/wifi/scan", HTTP_GET, wifi_scan, nullptr, false, false, nullptr},
      {"/api/v1/wifi", HTTP_POST, wifi_set, nullptr, false, false, nullptr},
      {"/api/v1/wifi/erase", HTTP_POST, wifi_erase, nullptr, false, false, nullptr},
      {"/api/v1/timezones", HTTP_GET, timezones, nullptr, false, false, nullptr},
      {"/api/v1/diag/log", HTTP_GET, diag_log, nullptr, false, false, nullptr},
      {"/api/v1/diag/dma", HTTP_POST, diag_dma, nullptr, false, false, nullptr},
      {"/api/v1/diag/memory", HTTP_GET, diag_memory, nullptr, false, false, nullptr},
      {"/api/v1/diag/bench", HTTP_GET, diag_bench, nullptr, false, false, nullptr},
  };
  for (const httpd_uri_t &r : routes) net::http::add(r);
  files::register_routes();
  content::register_routes();
  makapix_routes::register_routes();
  ws::register_routes();
  auth::register_routes();
  net::http::set_gate(auth::gate);
  ws::start();
  system::subscribe(system::Event::WifiConnected, [](const system::Message &) { ws::notify(); });
  system::subscribe(system::Event::WifiDisconnected, [](const system::Message &) { ws::notify(); });
  system::subscribe(system::Event::PlaybackSwapped, [](const system::Message &) { ws::notify(); });
  ESP_LOGI(TAG, "API v%d registered", kApiVersion);
}

void notify() { ws::notify(); }

}  // namespace p64::web

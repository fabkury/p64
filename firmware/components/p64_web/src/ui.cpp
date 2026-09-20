// p64 -- the embedded web UI (ADR 0005, spec 11): four pages, the shared stylesheet,
// scripts, the PWA manifest and icons, all in the firmware image. Every file goes out
// with an ETag of the firmware version, so browsers revalidate for free and never keep
// a stale page across an update. These routes are open (the pages show the PIN prompt
// themselves; the data behind them is what the PIN protects).
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "p64/net/http_server.hpp"
#include "p64/web/web.hpp"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char playsets_html_start[] asm("_binary_playsets_html_start");
extern const char playsets_html_end[] asm("_binary_playsets_html_end");
extern const char settings_html_start[] asm("_binary_settings_html_start");
extern const char settings_html_end[] asm("_binary_settings_html_end");
extern const char update_html_start[] asm("_binary_update_html_start");
extern const char update_html_end[] asm("_binary_update_html_end");
extern const char common_css_start[] asm("_binary_common_css_start");
extern const char common_css_end[] asm("_binary_common_css_end");
extern const char theme_js_start[] asm("_binary_theme_js_start");
extern const char theme_js_end[] asm("_binary_theme_js_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char manifest_json_start[] asm("_binary_manifest_json_start");
extern const char manifest_json_end[] asm("_binary_manifest_json_end");
extern const unsigned char icon_192_png_start[] asm("_binary_icon_192_png_start");
extern const unsigned char icon_192_png_end[] asm("_binary_icon_192_png_end");
extern const unsigned char icon_512_png_start[] asm("_binary_icon_512_png_start");
extern const unsigned char icon_512_png_end[] asm("_binary_icon_512_png_end");

namespace p64::web {
namespace {

struct Asset {
  const char *uri;
  const char *type;
  const char *start;
  const char *end;
  bool text;  // EMBED_TXTFILES adds a terminating NUL that must not go out
};

const Asset kAssets[] = {
    {"/", "text/html; charset=utf-8", index_html_start, index_html_end, true},
    {"/playsets", "text/html; charset=utf-8", playsets_html_start, playsets_html_end, true},
    {"/settings", "text/html; charset=utf-8", settings_html_start, settings_html_end, true},
    {"/update", "text/html; charset=utf-8", update_html_start, update_html_end, true},
    {"/static/common.css", "text/css; charset=utf-8", common_css_start, common_css_end, true},
    {"/static/theme.js", "application/javascript; charset=utf-8", theme_js_start, theme_js_end, true},
    {"/static/app.js", "application/javascript; charset=utf-8", app_js_start, app_js_end, true},
    {"/manifest.json", "application/manifest+json", manifest_json_start, manifest_json_end, true},
    {"/static/icon-192.png", "image/png", reinterpret_cast<const char *>(icon_192_png_start),
     reinterpret_cast<const char *>(icon_192_png_end), false},
    {"/static/icon-512.png", "image/png", reinterpret_cast<const char *>(icon_512_png_start),
     reinterpret_cast<const char *>(icon_512_png_end), false},
    {"/favicon.png", "image/png", reinterpret_cast<const char *>(icon_192_png_start),
     reinterpret_cast<const char *>(icon_192_png_end), false},
};

char g_etag[96] = {};  // "version date time" fits with room

esp_err_t serve(httpd_req_t *req, const Asset &a) {
  char match[96] = {};
  if (httpd_req_get_hdr_value_str(req, "If-None-Match", match, sizeof(match)) == ESP_OK && std::strcmp(match, g_etag) == 0) {
    httpd_resp_set_status(req, "304 Not Modified");
    httpd_resp_set_hdr(req, "ETag", g_etag);
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, a.type);
  httpd_resp_set_hdr(req, "ETag", g_etag);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  const size_t len = static_cast<size_t>(a.end - a.start) - (a.text ? 1 : 0);
  return httpd_resp_send(req, a.start, len);
}

esp_err_t asset_handler(httpd_req_t *req) { return serve(req, *static_cast<const Asset *>(req->user_ctx)); }

}  // namespace

esp_err_t ui_handler(httpd_req_t *req) { return serve(req, kAssets[0]); }

namespace ui {

void register_routes() {
  // The version alone stays the same across development builds: the build date and
  // time make every image distinct.
  const esp_app_desc_t *app = esp_app_get_description();
  std::snprintf(g_etag, sizeof(g_etag), "\"%s %s %s\"", app->version, app->date, app->time);
  for (const Asset &a : kAssets) {
    if (std::strcmp(a.uri, "/") == 0) continue;  // the setup portal owns "/" and calls ui_handler
    httpd_uri_t r = {};
    r.uri = a.uri;
    r.method = HTTP_GET;
    r.handler = asset_handler;
    r.user_ctx = const_cast<Asset *>(&a);
    net::http::add(r, true);
  }
}

}  // namespace ui
}  // namespace p64::web

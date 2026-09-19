// Serves the embedded web UI (ui/index.html, built into the app image: docs/adr/0005).

#include "esp_log.h"
#include "p64/net/http_server.hpp"
#include "p64/web/web.hpp"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

namespace p64::web {

esp_err_t ui_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

}  // namespace p64::web

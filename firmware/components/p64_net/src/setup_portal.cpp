#include "p64/net/setup_portal.hpp"

#include <string>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p64/net/http_server.hpp"
#include "p64/net/wifi.hpp"
#include "p64/system/settings.hpp"

namespace p64::net::portal {
namespace {

constexpr const char *TAG = "portal";
esp_err_t (*g_root_when_running)(httpd_req_t *) = nullptr;

// The setup page: small, inline styles, no external assets (the phone has no internet
// while it is joined to the setup network). Placeholders: {HOSTNAME}, {SSID}.
const char kPage[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>p64 Wi-Fi setup</title>
<style>
:root{color-scheme:dark}body{margin:0;background:#0e0a1a;color:#eee;font:16px/1.45 system-ui,sans-serif}
main{max-width:480px;margin:0 auto;padding:24px 16px}h1{font-size:28px;margin:0 0 4px;background:linear-gradient(90deg,#ff4d97,#8b5cff,#33a6ff);-webkit-background-clip:text;color:transparent}
.sub{color:#9a93b3;margin:0 0 20px}.card{background:#171229;border-radius:12px;padding:16px;margin-bottom:16px}
label{display:block;font-size:13px;color:#9a93b3;margin:12px 0 4px}input,select{width:100%;box-sizing:border-box;background:#0e0a1a;color:#eee;border:1px solid #2c2544;border-radius:8px;padding:10px;font-size:16px}
button{width:100%;margin-top:16px;background:#8b5cff;color:#fff;border:0;border-radius:8px;padding:12px;font-size:16px;cursor:pointer}button.secondary{background:#2c2544}button.danger{background:#7a2130}
.hint{font-size:13px;color:#9a93b3;margin-top:8px}.row{display:flex;gap:8px}.row button{margin-top:4px}
</style></head><body><main>
<h1>p64</h1><p class="sub">Wi-Fi setup</p>
<div class="card"><form method="post" action="/setup/save">
<label for="ssid">Network name (SSID)</label>
<div class="row"><input id="ssid" name="ssid" list="nets" maxlength="32" required placeholder="e.g. MyHomeWiFi" value="{SSID}"><button type="button" class="secondary" style="width:auto;padding:10px 14px" onclick="scan()">Scan</button></div>
<datalist id="nets"></datalist>
<label for="password">Password (leave empty for an open network)</label>
<input id="password" name="password" type="password" maxlength="64" autocomplete="off">
<label for="device_name">Device name (optional)</label>
<input id="device_name" name="device_name" maxlength="16" pattern="[a-z0-9-]*" placeholder="e.g. bedroom" oninput="preview()">
<p class="hint">Lowercase letters, digits and hyphens. The device is then reachable as <b id="host">{HOSTNAME}.local</b>. Useful only with more than one p64.</p>
<button type="submit">Save and connect</button>
</form>
<p class="hint">After saving, p64 joins your network and this setup network disappears. Then open <b>http://<span id="host2">{HOSTNAME}</span>.local/</b> from any device on the same Wi-Fi.</p></div>
<div class="card"><form method="post" action="/setup/erase" onsubmit="return confirm('Forget the saved Wi-Fi network?')"><button class="danger">Erase Wi-Fi credentials</button></form></div>
<script>
function preview(){var n=document.getElementById('device_name').value;var h=n?'p64-'+n:'p64';document.getElementById('host').textContent=h+'.local';document.getElementById('host2').textContent=h}
function scan(){var b=event.target;b.textContent='...';fetch('/setup/scan').then(r=>r.json()).then(list=>{var d=document.getElementById('nets');d.innerHTML='';list.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.label=n.ssid+' ('+n.rssi+' dBm'+(n.secure?'':', open')+')';d.appendChild(o)});b.textContent='Scan ('+list.length+')'}).catch(function(){b.textContent='Scan'})}
</script></main></body></html>)HTML";

const char kSaved[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>p64: saved</title>
<style>body{margin:0;background:#0e0a1a;color:#eee;font:16px/1.5 system-ui,sans-serif}main{max-width:480px;margin:0 auto;padding:24px 16px}.card{background:#171229;border-radius:12px;padding:16px}a{color:#33a6ff}</style></head>
<body><main><div class="card"><h2>Saved</h2><p>p64 is joining <b>{SSID}</b>. This setup network closes as soon as it connects.</p>
<p>Reconnect your phone or computer to your own Wi-Fi, then open <a href="http://{HOSTNAME}.local/">http://{HOSTNAME}.local/</a>.</p>
<p>If p64 cannot join (wrong password, network out of reach), the <b>p64-setup</b> network comes back within a minute.</p></div></main></body></html>)HTML";

const char kErased[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>p64: erased</title>
<style>body{margin:0;background:#0e0a1a;color:#eee;font:16px/1.5 system-ui,sans-serif}main{max-width:480px;margin:0 auto;padding:24px 16px}</style></head>
<body><main><h2>Wi-Fi credentials erased</h2><p>p64 stays in setup mode. <a style="color:#33a6ff" href="/">Back to setup</a>.</p></main></body></html>)HTML";

const char kRunning[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><title>p64</title>
<style>body{margin:0;background:#0e0a1a;color:#eee;font:16px/1.5 system-ui,sans-serif}main{max-width:480px;margin:0 auto;padding:24px 16px}</style></head>
<body><main><h1>p64</h1><p>The device is running. The web interface arrives with a later firmware milestone.</p></main></body></html>)HTML";

std::string replace_all(std::string s, const std::string &from, const std::string &to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

std::string url_decode(const std::string &in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size()) {
      const std::string hex = in.substr(i + 1, 2);
      out += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

std::string form_field(const std::string &body, const std::string &name) {
  size_t pos = 0;
  while (pos <= body.size()) {
    const size_t amp = body.find('&', pos);
    const std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    const size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.substr(0, eq) == name) return url_decode(pair.substr(eq + 1));
    if (amp == std::string::npos) break;
    pos = amp + 1;
  }
  return "";
}

std::string html_escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

esp_err_t send_page(httpd_req_t *req) {
  const wifi::Status st = wifi::status();
  std::string page = replace_all(kPage, "{HOSTNAME}", st.hostname);
  page = replace_all(page, "{SSID}", html_escape(st.ssid));
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, page.c_str());
}

esp_err_t root_handler(httpd_req_t *req) {
  if (wifi::status().setup_mode) return send_page(req);
  if (g_root_when_running) return g_root_when_running(req);
  return http::send(req, "200 OK", "text/html; charset=utf-8", kRunning);
}

esp_err_t setup_handler(httpd_req_t *req) { return send_page(req); }

esp_err_t scan_handler(httpd_req_t *req) {
  cJSON *arr = cJSON_CreateArray();
  for (const wifi::ScanEntry &e : wifi::scan()) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", e.ssid.c_str());
    cJSON_AddNumberToObject(o, "rssi", e.rssi);
    cJSON_AddBoolToObject(o, "secure", e.secure);
    cJSON_AddItemToArray(arr, o);
  }
  char *text = cJSON_PrintUnformatted(arr);
  const esp_err_t r = http::send(req, "200 OK", "application/json", text ? text : "[]");
  cJSON_free(text);
  cJSON_Delete(arr);
  return r;
}

// Saving or erasing credentials changes the network the request may have come over, so
// the reply goes out first and the action runs half a second later on its own task.
struct DeferredAction {
  bool erase;
  std::string ssid, password;
};

void deferred_task(void *arg) {
  auto *a = static_cast<DeferredAction *>(arg);
  vTaskDelay(pdMS_TO_TICKS(500));
  if (a->erase) {
    wifi::erase_credentials();
  } else if (!wifi::save_credentials(a->ssid, a->password)) {
    ESP_LOGE(TAG, "could not save the credentials");
  }
  delete a;
  vTaskDelete(nullptr);
}

void run_deferred(DeferredAction *a) {
  if (xTaskCreatePinnedToCore(deferred_task, "portal_act", 4096, a, 5, nullptr, 0) != pdPASS) {
    ESP_LOGE(TAG, "could not start the deferred action");
    delete a;
  }
}

esp_err_t save_handler(httpd_req_t *req) {
  std::string body;
  if (!http::read_body(req, body, 1024)) return http::send(req, "400 Bad Request", "text/plain", "body too large");
  const std::string ssid = form_field(body, "ssid");
  const std::string password = form_field(body, "password");
  const std::string device_name = form_field(body, "device_name");
  if (ssid.empty() || ssid.size() > 32) return http::send(req, "400 Bad Request", "text/plain", "SSID required (up to 32 characters)");
  if (password.size() > 64) return http::send(req, "400 Bad Request", "text/plain", "password up to 64 characters");
  system::settings_update([&](system::Settings &s) { s.device_name = device_name; });
  const std::string hostname = system::settings().hostname();
  wifi::set_hostname(hostname);
  ESP_LOGI(TAG, "credentials received from the portal (ssid \"%s\", hostname %s)", ssid.c_str(), hostname.c_str());
  std::string page = replace_all(kSaved, "{SSID}", html_escape(ssid));
  page = replace_all(page, "{HOSTNAME}", hostname);
  const esp_err_t r = http::send(req, "200 OK", "text/html; charset=utf-8", page.c_str());
  run_deferred(new DeferredAction{false, ssid, password});
  return r;
}

esp_err_t erase_handler(httpd_req_t *req) {
  const esp_err_t r = http::send(req, "200 OK", "text/html; charset=utf-8", kErased);
  run_deferred(new DeferredAction{true, "", ""});
  return r;
}

// Captive-portal probes: every OS asks a known URL and expects a specific answer; a
// redirect to the portal makes it open the sign-in sheet.
esp_err_t probe_handler(httpd_req_t *req) {
  if (!wifi::status().setup_mode) return http::send(req, "204 No Content", "text/plain", "");
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, "<a href=\"http://192.168.4.1/\">p64 setup</a>");
}

}  // namespace

void init(esp_err_t (*root_when_running)(httpd_req_t *)) {
  g_root_when_running = root_when_running;
  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, root_handler, nullptr, false, false, nullptr},
      {"/setup", HTTP_GET, setup_handler, nullptr, false, false, nullptr},
      {"/setup/scan", HTTP_GET, scan_handler, nullptr, false, false, nullptr},
      {"/setup/save", HTTP_POST, save_handler, nullptr, false, false, nullptr},
      {"/setup/erase", HTTP_POST, erase_handler, nullptr, false, false, nullptr},
      {"/generate_204", HTTP_GET, probe_handler, nullptr, false, false, nullptr},   // Android
      {"/gen_204", HTTP_GET, probe_handler, nullptr, false, false, nullptr},        // Android
      {"/hotspot-detect.html", HTTP_GET, probe_handler, nullptr, false, false, nullptr},  // Apple
      {"/library/test/success.html", HTTP_GET, probe_handler, nullptr, false, false, nullptr},  // Apple
      {"/connecttest.txt", HTTP_GET, probe_handler, nullptr, false, false, nullptr},  // Windows
      {"/ncsi.txt", HTTP_GET, probe_handler, nullptr, false, false, nullptr},        // Windows
      {"/redirect", HTTP_GET, probe_handler, nullptr, false, false, nullptr},        // Windows
      {"/canonical.html", HTTP_GET, probe_handler, nullptr, false, false, nullptr},  // Firefox
      {"/success.txt", HTTP_GET, probe_handler, nullptr, false, false, nullptr},     // Firefox
  };
  for (const httpd_uri_t &r : routes) http::add(r);
}

}  // namespace p64::net::portal

#include "net/web.hpp"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "sdkconfig.h"

#include "net/makapix.hpp"
#include "net/wifi.hpp"
#include "sdcard.hpp"

namespace p64::web {
namespace {

constexpr const char *TAG = "web";
constexpr uint32_t kDefaultSeconds = CONFIG_P64_WEB_DEFAULT_SECONDS;
constexpr uint32_t kPlaylistDefaultSeconds = CONFIG_P64_GIF_DWELL_S;
constexpr uint32_t kMaxSeconds = 7 * 24 * 3600;
constexpr size_t kMaxQueryBytes = 1024;  // query string or form body
constexpr size_t kMaxUrlChars = 400;
constexpr size_t kMaxUploadBytes = CONFIG_P64_WEB_MAX_BYTES;
constexpr size_t kIoChunk = 16 * 1024;

struct Playlist {
  std::vector<std::string> files;
  uint32_t seconds = 0;
};

httpd_handle_t g_server = nullptr;
std::mutex g_mutex;
NowPlaying g_now;
std::optional<Playlist> g_playlist;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_pattern{false};

const char kIndexHtml[] = R"html(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>p64</title>
<style>
body{font:16px system-ui,sans-serif;max-width:34em;margin:2em auto;padding:0 1em;color:#222}
fieldset{border:1px solid #bbb;border-radius:6px;margin:0 0 1.5em;padding:1em}
label{display:block;margin:.6em 0 .2em}
input[type=text]{width:100%;box-sizing:border-box;font-size:1em;padding:.4em}
input[type=number]{width:7em;font-size:1em;padding:.4em}
button{font-size:1em;padding:.4em 1em;margin:.2em .2em .2em 0}
#drop{border:2px dashed #999;border-radius:6px;padding:1.2em;text-align:center;color:#555;margin:.6em 0}
#drop.over{border-color:#06c;color:#06c}
#files{list-style:none;padding:0}#files li{padding:.3em 0;border-bottom:1px solid #eee}
#files a{word-break:break-all}
</style></head>
<body><h1>p64</h1>
<form action="/play" method="get"><fieldset><legend>Makapix Club post</legend>
<label for="post">Post URL or sqid</label><input id="post" type="text" name="post" placeholder="https://makapix.club/p/7fQw or 7fQw" required>
<label for="s1">Seconds (0 = until the next request)</label><input id="s1" type="number" name="seconds" value="60" min="0">
<br><button type="submit">Play</button></fieldset></form>
<form action="/play" method="get"><fieldset><legend>Any GIF URL</legend>
<label for="url">URL</label><input id="url" type="text" name="url" placeholder="https://example.com/animation.gif" required>
<label for="s2">Seconds (0 = until the next request)</label><input id="s2" type="number" name="seconds" value="60" min="0">
<br><button type="submit">Play</button></fieldset></form>
<fieldset><legend>microSD card</legend>
<p id="sdinfo">Reading the card...</p>
<div id="drop">Drop GIF files here to copy them to the card, or <input type="file" id="pick" accept=".gif" multiple></div>
<p><button onclick="playAll()">Play all, 30 s each</button> <button onclick="fetch('/stop')">Stop</button> <button onclick="fetch('/sd/mount').then(refresh)">Remount card</button></p>
<ul id="files"></ul>
</fieldset>
<p><a href="/status">Status</a> &middot; <a href="/stop">Stop and resume the show</a> &middot; <a href="/pattern">Tone test pattern</a></p>
<script>
function el(t,c){var e=document.createElement(t);if(c!==undefined)e.textContent=c;return e;}
function refresh(){fetch('/sd').then(function(r){return r.json();}).then(function(d){
 var info=document.getElementById('sdinfo');var ul=document.getElementById('files');ul.textContent='';
 if(!d.mounted){info.textContent='No card mounted'+(d.error?': '+d.error:'')+'.';return;}
 info.textContent=d.card+': '+d.free_mb+' MB free of '+d.total_mb+' MB, '+d.files.length+' GIF file(s).';
 d.files.forEach(function(f){var li=el('li');var a=el('a',f.name);a.href='/sd/'+encodeURIComponent(f.name);li.appendChild(a);
  li.appendChild(el('span',' ('+Math.max(1,Math.round(f.size/1024))+' KB) '));
  var p=el('button','Play');p.onclick=function(){fetch('/play?file='+encodeURIComponent(f.name));};li.appendChild(p);
  var x=el('button','Delete');x.onclick=function(){if(confirm('Delete '+f.name+' from the card?'))fetch('/sd/'+encodeURIComponent(f.name),{method:'DELETE'}).then(refresh);};li.appendChild(x);
  ul.appendChild(li);});
}).catch(function(){document.getElementById('sdinfo').textContent='Could not read the card listing.';});}
function upload(files){var i=0;function next(){if(i>=files.length){refresh();return;}var f=files[i++];
 document.getElementById('sdinfo').textContent='Copying '+f.name+' ('+Math.round(f.size/1024)+' KB)...';
 fetch('/sd/'+encodeURIComponent(f.name),{method:'PUT',body:f}).then(next,next);}next();}
var drop=document.getElementById('drop');
drop.ondragover=function(e){e.preventDefault();drop.className='over';};
drop.ondragleave=function(){drop.className='';};
drop.ondrop=function(e){e.preventDefault();drop.className='';upload(e.dataTransfer.files);};
document.getElementById('pick').onchange=function(e){upload(e.target.files);};
function playAll(){fetch('/sd/play');}
refresh();
</script>
</body></html>
)html";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string url_decode(const char *s) {
  std::string out;
  for (; *s; ++s) {
    if (*s == '+') {
      out += ' ';
    } else if (*s == '%' && std::isxdigit(static_cast<unsigned char>(s[1])) &&
               std::isxdigit(static_cast<unsigned char>(s[2]))) {
      const char hex[3] = {s[1], s[2], '\0'};
      out += static_cast<char>(std::strtol(hex, nullptr, 16));
      s += 2;
    } else {
      out += *s;
    }
  }
  return out;
}

// The query string of a GET, or the form-encoded body of a POST.
bool read_query(httpd_req_t *req, std::string &query, const char *&error) {
  if (req->method == HTTP_POST) {
    if (req->content_len == 0 || req->content_len >= kMaxQueryBytes) {
      error = "form body missing or too long";
      return false;
    }
    query.resize(req->content_len);
    size_t got = 0;
    while (got < query.size()) {
      const int n = httpd_req_recv(req, query.data() + got, query.size() - got);
      if (n <= 0) {
        error = "could not read the body";
        return false;
      }
      got += static_cast<size_t>(n);
    }
    return true;
  }
  const size_t len = httpd_req_get_url_query_len(req);
  if (len == 0) {
    error = "missing parameters: post=<Makapix post URL or sqid>, url=<GIF URL> or file=<card file>, optional seconds=N";
    return false;
  }
  if (len >= kMaxQueryBytes) {
    error = "query string too long";
    return false;
  }
  query.resize(len + 1);
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
    error = "bad query string";
    return false;
  }
  query.resize(std::strlen(query.c_str()));
  return true;
}

// Query string of any request (empty when there is none).
std::string query_of(httpd_req_t *req) {
  const size_t len = httpd_req_get_url_query_len(req);
  if (len == 0 || len >= kMaxQueryBytes) return "";
  std::string q(len + 1, '\0');
  if (httpd_req_get_url_query_str(req, q.data(), q.size()) != ESP_OK) return "";
  q.resize(std::strlen(q.c_str()));
  return q;
}

bool get_param(const std::string &query, const char *key, std::string &out) {
  char buf[kMaxQueryBytes];
  if (query.empty() || httpd_query_key_value(query.c_str(), key, buf, sizeof(buf)) != ESP_OK) return false;
  out = url_decode(buf);
  return true;
}

esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *root) {
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const esp_err_t rc = httpd_resp_sendstr(req, text ? text : "{}");
  cJSON_free(text);
  return rc;
}

esp_err_t send_error(httpd_req_t *req, const char *status, const char *message) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "error", message);
  return send_json(req, status, root);
}

// "7fQw", "https://makapix.club/p/7fQw", "makapix.club/p/7fQw?x=1" -> "7fQw"; "" when it
// is neither a post URL nor a plausible sqid.
std::string parse_sqid(const std::string &post) {
  std::string s = post;
  const size_t p = s.find("/p/");
  if (p != std::string::npos) {
    s = s.substr(p + 3);
  } else if (s.find_first_of("/:") != std::string::npos) {
    return "";
  }
  const size_t end = s.find_first_of("/?#");
  if (end != std::string::npos) s.resize(end);
  if (s.empty() || s.size() > 32) return "";
  for (const char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return "";
  }
  return s;
}

bool parse_seconds(const std::string &text, uint32_t &seconds) {
  if (text.empty()) return false;
  char *end = nullptr;
  const unsigned long v = std::strtoul(text.c_str(), &end, 10);
  if (*end != '\0' || v > kMaxSeconds) return false;
  seconds = static_cast<uint32_t>(v);
  return true;
}

const char *state_name(makapix::PlayState s) {
  switch (s) {
    case makapix::PlayState::idle:
      return "idle";
    case makapix::PlayState::queued:
      return "queued";
    case makapix::PlayState::downloading:
      return "downloading";
    case makapix::PlayState::ready:
      return "ready";
    case makapix::PlayState::failed:
      return "failed";
  }
  return "?";
}

bool has_gif_extension(const std::string &name) {
  if (name.size() < 4) return false;
  std::string ext = name.substr(name.size() - 4);
  for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".gif";
}

// The file name in "/sd/<name>[?...]".
std::string sd_name_from_uri(httpd_req_t *req) {
  std::string uri = req->uri;
  const size_t q = uri.find('?');
  if (q != std::string::npos) uri.resize(q);
  if (uri.rfind("/sd/", 0) != 0) return "";
  return url_decode(uri.c_str() + 4);
}

void add_sd_info(cJSON *root, bool with_files) {
  const sdcard::Info i = sdcard::info();
  cJSON *sd = cJSON_AddObjectToObject(root, "sd");
  cJSON_AddBoolToObject(sd, "mounted", i.mounted);
  if (!i.error.empty()) cJSON_AddStringToObject(sd, "error", i.error.c_str());
  if (i.mounted) {
    cJSON_AddStringToObject(sd, "card", i.card.c_str());
    cJSON_AddNumberToObject(sd, "total_mb", static_cast<double>(i.total >> 20));
    cJSON_AddNumberToObject(sd, "free_mb", static_cast<double>(i.free >> 20));
    const std::vector<sdcard::FileInfo> files = sdcard::list_gifs();
    cJSON_AddNumberToObject(sd, "gifs", static_cast<double>(files.size()));
    if (with_files) {
      cJSON *arr = cJSON_AddArrayToObject(sd, "files");
      for (const sdcard::FileInfo &f : files) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", f.name.c_str());
        cJSON_AddNumberToObject(o, "size", static_cast<double>(f.size));
        cJSON_AddItemToArray(arr, o);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Handlers: playback
// ---------------------------------------------------------------------------

esp_err_t handle_index(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_play(httpd_req_t *req) {
  std::string query;
  const char *error = nullptr;
  if (!read_query(req, query, error)) return send_error(req, "400 Bad Request", error);

  std::string post, url, file, seconds_text;
  const bool has_post = get_param(query, "post", post);
  const bool has_url = get_param(query, "url", url);
  const bool has_file = get_param(query, "file", file);
  if ((has_post ? 1 : 0) + (has_url ? 1 : 0) + (has_file ? 1 : 0) != 1) {
    return send_error(req, "400 Bad Request", "give exactly one of post=..., url=... or file=...");
  }

  makapix::PlayRequest r;
  r.seconds = kDefaultSeconds;
  if (get_param(query, "seconds", seconds_text) && !parse_seconds(seconds_text, r.seconds)) {
    return send_error(req, "400 Bad Request", "seconds must be a whole number (0 = until the next request)");
  }
  if (has_post) {
    r.sqid = parse_sqid(post);
    if (r.sqid.empty()) {
      return send_error(req, "400 Bad Request",
                        "post must be a Makapix Club post URL (https://makapix.club/p/<sqid>) or a sqid");
    }
  } else if (has_url) {
    const bool http = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
    if (!http || url.size() > kMaxUrlChars || url.find_first_of(" \t\r\n\"<>") != std::string::npos) {
      return send_error(req, "400 Bad Request", "url must be an http:// or https:// URL of a GIF file");
    }
    r.url = url;
  } else {
    if (!sdcard::valid_name(file)) return send_error(req, "400 Bad Request", "file must be a plain file name on the card");
    if (!sdcard::mounted()) return send_error(req, "503 Service Unavailable", "no card mounted");
    r.file = file;
  }
  if (!has_file && !wifi::connected()) return send_error(req, "503 Service Unavailable", "Wi-Fi is not connected");

  const uint32_t id = makapix::request_play(r);
  if (id == 0) return send_error(req, "503 Service Unavailable", "the fetcher task is not running");
  const std::string how = r.seconds ? std::to_string(r.seconds) + " s" : "until the next request";
  const char *kind = has_post ? "makapix " : (has_file ? "card file " : "");
  const std::string &target = has_post ? r.sqid : (has_file ? r.file : r.url);
  ESP_LOGI(TAG, "play request #%lu: %s%s, %s", static_cast<unsigned long>(id), kind, target.c_str(), how.c_str());

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "queued", true);
  cJSON_AddNumberToObject(root, "id", id);
  if (has_post) {
    cJSON_AddStringToObject(root, "sqid", r.sqid.c_str());
    const std::string page = std::string("https://") + CONFIG_P64_MAKAPIX_HOST + "/p/" + r.sqid;
    cJSON_AddStringToObject(root, "page", page.c_str());
  } else if (has_url) {
    cJSON_AddStringToObject(root, "url", r.url.c_str());
  } else {
    cJSON_AddStringToObject(root, "file", r.file.c_str());
  }
  cJSON_AddNumberToObject(root, "seconds", r.seconds);
  cJSON_AddStringToObject(root, "status", "/status");
  return send_json(req, "202 Accepted", root);
}

esp_err_t handle_stop(httpd_req_t *req) {
  makapix::cancel_play();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_playlist.reset();
  }
  g_stop = true;
  ESP_LOGI(TAG, "stop requested");
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "stopped", true);
  return send_json(req, "200 OK", root);
}

esp_err_t handle_pattern(httpd_req_t *req) {
  makapix::cancel_play();
  g_pattern = true;
  ESP_LOGI(TAG, "test pattern requested");
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "pattern", true);
  cJSON_AddStringToObject(root, "layout",
                          "rows 8-15 grey 0..63 by column; 16-23 grey 0..255; 24-31 red, 32-39 green, 40-47 blue "
                          "0..63; 48-63 a brown and a grey sphere; /stop or /play ends it");
  return send_json(req, "200 OK", root);
}

esp_err_t handle_status(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "hostname", CONFIG_P64_WEB_HOSTNAME ".local");
  cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
  cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000));

  cJSON *wifi_obj = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddBoolToObject(wifi_obj, "connected", wifi::connected());
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip = {};
  if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
    char text[16];
    std::snprintf(text, sizeof(text), IPSTR, IP2STR(&ip.ip));
    cJSON_AddStringToObject(wifi_obj, "ip", text);
  }
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) cJSON_AddNumberToObject(wifi_obj, "rssi", ap.rssi);

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    cJSON *playing = cJSON_AddObjectToObject(root, "playing");
    cJSON_AddStringToObject(playing, "name", g_now.name.c_str());
    cJSON_AddStringToObject(playing, "source", g_now.source.c_str());
    if (!g_now.url.empty()) cJSON_AddStringToObject(playing, "url", g_now.url.c_str());
    cJSON_AddNumberToObject(playing, "width", g_now.width);
    cJSON_AddNumberToObject(playing, "height", g_now.height);
    cJSON_AddNumberToObject(playing, "bytes", static_cast<double>(g_now.bytes));
    cJSON_AddBoolToObject(playing, "on_demand", g_now.on_demand);
    const int64_t now_us = esp_timer_get_time();
    if (g_now.started_us) {
      cJSON_AddNumberToObject(playing, "since_s", static_cast<double>((now_us - g_now.started_us) / 1000000));
    }
    if (g_now.on_demand) {
      if (g_now.until_us == 0) {
        cJSON_AddBoolToObject(playing, "indefinite", true);
      } else {
        const int64_t left = g_now.until_us - now_us;
        cJSON_AddNumberToObject(playing, "seconds_left",
                                static_cast<double>(left > 0 ? (left + 999999) / 1000000 : 0));
      }
    }
    if (g_now.playlist_count > 0) {
      cJSON *pl = cJSON_AddObjectToObject(root, "playlist");
      cJSON_AddNumberToObject(pl, "index", g_now.playlist_index);
      cJSON_AddNumberToObject(pl, "count", g_now.playlist_count);
    }
  }

  const makapix::PlayStatus ps = makapix::play_status();
  cJSON *request = cJSON_AddObjectToObject(root, "request");
  cJSON_AddNumberToObject(request, "id", ps.id);
  cJSON_AddStringToObject(request, "state", state_name(ps.state));
  if (!ps.target.empty()) cJSON_AddStringToObject(request, "target", ps.target.c_str());
  if (!ps.error.empty()) cJSON_AddStringToObject(request, "error", ps.error.c_str());

  add_sd_info(root, false);

  cJSON *heap = cJSON_AddObjectToObject(root, "heap");
  cJSON_AddNumberToObject(heap, "free", static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT)));
  cJSON_AddNumberToObject(heap, "free_internal",
                          static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  return send_json(req, "200 OK", root);
}

// ---------------------------------------------------------------------------
// Handlers: microSD card
// ---------------------------------------------------------------------------

esp_err_t handle_sd_list(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  add_sd_info(root, true);
  // Flatten: the page and scripts read the card object directly.
  cJSON *sd = cJSON_DetachItemFromObject(root, "sd");
  cJSON_Delete(root);
  return send_json(req, "200 OK", sd);
}

esp_err_t handle_sd_play(httpd_req_t *req) {
  if (!sdcard::mounted()) return send_error(req, "503 Service Unavailable", "no card mounted");
  uint32_t seconds = kPlaylistDefaultSeconds;
  std::string seconds_text;
  if (get_param(query_of(req), "seconds", seconds_text) && (!parse_seconds(seconds_text, seconds) || seconds == 0)) {
    return send_error(req, "400 Bad Request", "seconds must be a whole number of at least 1");
  }
  const std::vector<sdcard::FileInfo> files = sdcard::list_gifs();
  if (files.empty()) return send_error(req, "404 Not Found", "no GIF files on the card");
  Playlist pl;
  pl.seconds = seconds;
  for (const sdcard::FileInfo &f : files) pl.files.push_back(f.name);
  const size_t count = pl.files.size();
  makapix::cancel_play();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_playlist = std::move(pl);
  }
  ESP_LOGI(TAG, "card playlist requested: %u files, %lu s each", static_cast<unsigned>(count),
           static_cast<unsigned long>(seconds));
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "playlist", true);
  cJSON_AddNumberToObject(root, "count", static_cast<double>(count));
  cJSON_AddNumberToObject(root, "seconds", seconds);
  return send_json(req, "202 Accepted", root);
}

esp_err_t handle_sd_mount(httpd_req_t *req) {
  const bool ok = sdcard::remount();
  ESP_LOGI(TAG, "card remount requested: %s", ok ? "mounted" : "failed");
  cJSON *root = cJSON_CreateObject();
  add_sd_info(root, false);
  return send_json(req, ok ? "200 OK" : "503 Service Unavailable", root);
}

esp_err_t sd_upload(httpd_req_t *req, const std::string &name) {
  if (!has_gif_extension(name)) return send_error(req, "400 Bad Request", "only .gif files go on the card");
  if (req->content_len == 0) return send_error(req, "411 Length Required", "send the file with a Content-Length");
  if (req->content_len > kMaxUploadBytes) return send_error(req, "413 Payload Too Large", "file larger than the limit");
  const std::string final_path = sdcard::path_of(name);
  const std::string part_path = final_path + ".part";
  FILE *f = std::fopen(part_path.c_str(), "wb");
  if (!f) {
    ESP_LOGW(TAG, "upload %s: cannot create the file (%s)", name.c_str(), std::strerror(errno));
    return send_error(req, "500 Internal Server Error", "cannot create the file on the card");
  }
  std::unique_ptr<char[]> buf(new char[kIoChunk]);
  const int64_t t0 = esp_timer_get_time();
  size_t remaining = req->content_len;
  uint8_t head[6] = {};
  size_t head_len = 0;
  bool ok = true;
  const char *why = "";
  while (remaining > 0) {
    const int n = httpd_req_recv(req, buf.get(), std::min(kIoChunk, remaining));
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      ok = false;
      why = "connection lost during the upload";
      break;
    }
    for (int i = 0; i < n && head_len < sizeof(head); ++i) head[head_len++] = static_cast<uint8_t>(buf[i]);
    if (std::fwrite(buf.get(), 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
      ok = false;
      why = "write to the card failed (card full?)";
      break;
    }
    remaining -= static_cast<size_t>(n);
  }
  std::fclose(f);
  if (ok && (head_len < 6 || std::memcmp(head, "GIF8", 4) != 0)) {
    ok = false;
    why = "the data is not a GIF file";
  }
  if (!ok) {
    unlink(part_path.c_str());
    ESP_LOGW(TAG, "upload %s failed: %s", name.c_str(), why);
    return send_error(req, "400 Bad Request", why);
  }
  unlink(final_path.c_str());  // FAT rename does not replace
  if (std::rename(part_path.c_str(), final_path.c_str()) != 0) {
    unlink(part_path.c_str());
    return send_error(req, "500 Internal Server Error", "cannot rename the uploaded file");
  }
  const int64_t ms = (esp_timer_get_time() - t0) / 1000;
  ESP_LOGI(TAG, "uploaded %s (%u bytes) to the card in %lld ms", name.c_str(), static_cast<unsigned>(req->content_len),
           static_cast<long long>(ms));
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "saved", name.c_str());
  cJSON_AddNumberToObject(root, "bytes", static_cast<double>(req->content_len));
  return send_json(req, "201 Created", root);
}

esp_err_t sd_download(httpd_req_t *req, const std::string &name) {
  FILE *f = std::fopen(sdcard::path_of(name).c_str(), "rb");
  if (!f) return send_error(req, "404 Not Found", "no such file on the card");
  httpd_resp_set_type(req, "image/gif");
  std::unique_ptr<char[]> buf(new char[kIoChunk]);
  while (true) {
    const size_t n = std::fread(buf.get(), 1, kIoChunk, f);
    if (n == 0) break;
    if (httpd_resp_send_chunk(req, buf.get(), static_cast<ssize_t>(n)) != ESP_OK) {
      std::fclose(f);
      return ESP_FAIL;
    }
  }
  std::fclose(f);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_sd_file(httpd_req_t *req) {
  const std::string name = sd_name_from_uri(req);
  if (!sdcard::valid_name(name)) return send_error(req, "400 Bad Request", "invalid file name");
  if (!sdcard::mounted()) return send_error(req, "503 Service Unavailable", "no card mounted");
  if (req->method == HTTP_PUT) return sd_upload(req, name);
  if (req->method == HTTP_DELETE) {
    std::string error;
    if (!sdcard::remove_file(name, error)) return send_error(req, "404 Not Found", error.c_str());
    ESP_LOGI(TAG, "deleted %s from the card", name.c_str());
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "deleted", name.c_str());
    return send_json(req, "200 OK", root);
  }
  return sd_download(req, name);
}

void start_mdns() {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
    return;
  }
  mdns_hostname_set(CONFIG_P64_WEB_HOSTNAME);
  mdns_instance_name_set("p64 LED matrix");
  mdns_txt_item_t txt[] = {{"path", "/"}};
  err = mdns_service_add(nullptr, "_http", "_tcp", CONFIG_P64_WEB_PORT, txt, 1);
  if (err != ESP_OK) ESP_LOGW(TAG, "mDNS service add failed: %s", esp_err_to_name(err));
}

}  // namespace

void start() {
#if !defined(CONFIG_P64_WEB_ENABLE)
  ESP_LOGI(TAG, "disabled in menuconfig");
  return;
#endif
  if (g_server) return;
  start_mdns();

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = CONFIG_P64_WEB_PORT;
  cfg.stack_size = 10 * 1024;  // handlers use std::string and cJSON
  cfg.core_id = 0;             // keep the network side away from the rendering core
  cfg.lru_purge_enable = true;
  cfg.max_uri_handlers = 16;
  cfg.uri_match_fn = httpd_uri_match_wildcard;  // for /sd/<name>
  const esp_err_t err = httpd_start(&g_server, &cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP server failed to start: %s", esp_err_to_name(err));
    g_server = nullptr;
    return;
  }
  // Exact routes first: the matcher takes the first registered handler that fits.
  const httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = handle_index, .user_ctx = nullptr},
      {.uri = "/play", .method = HTTP_GET, .handler = handle_play, .user_ctx = nullptr},
      {.uri = "/play", .method = HTTP_POST, .handler = handle_play, .user_ctx = nullptr},
      {.uri = "/stop", .method = HTTP_GET, .handler = handle_stop, .user_ctx = nullptr},
      {.uri = "/pattern", .method = HTTP_GET, .handler = handle_pattern, .user_ctx = nullptr},
      {.uri = "/status", .method = HTTP_GET, .handler = handle_status, .user_ctx = nullptr},
      {.uri = "/sd", .method = HTTP_GET, .handler = handle_sd_list, .user_ctx = nullptr},
      {.uri = "/sd/play", .method = HTTP_GET, .handler = handle_sd_play, .user_ctx = nullptr},
      {.uri = "/sd/mount", .method = HTTP_GET, .handler = handle_sd_mount, .user_ctx = nullptr},
      {.uri = "/sd/*", .method = HTTP_GET, .handler = handle_sd_file, .user_ctx = nullptr},
      {.uri = "/sd/*", .method = HTTP_PUT, .handler = handle_sd_file, .user_ctx = nullptr},
      {.uri = "/sd/*", .method = HTTP_DELETE, .handler = handle_sd_file, .user_ctx = nullptr},
  };
  for (const httpd_uri_t &r : routes) httpd_register_uri_handler(g_server, &r);
  ESP_LOGI(TAG,
           "web control at http://%s.local:%d/ (/play, /stop, /status, /pattern, /sd); default %lu s per request, "
           "GIFs up to %dx%d and %u bytes",
           CONFIG_P64_WEB_HOSTNAME, CONFIG_P64_WEB_PORT, static_cast<unsigned long>(kDefaultSeconds),
           CONFIG_P64_WEB_MAX_DIMENSION, CONFIG_P64_WEB_MAX_DIMENSION, static_cast<unsigned>(CONFIG_P64_WEB_MAX_BYTES));
}

void publish(const NowPlaying &now) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_now = now;
}

bool take_stop() { return g_stop.exchange(false); }

bool take_pattern() { return g_pattern.exchange(false); }

bool take_playlist(std::vector<std::string> &files, uint32_t &seconds) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_playlist) return false;
  files = std::move(g_playlist->files);
  seconds = g_playlist->seconds;
  g_playlist.reset();
  return true;
}

}  // namespace p64::web

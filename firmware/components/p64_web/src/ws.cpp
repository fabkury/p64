// The WebSocket status push: clients connect to /api/v1/ws and receive the status
// document as JSON text every 2 s and at once when something changes (spec 11.2).

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "p64/net/http_server.hpp"
#include "p64/web/web.hpp"

namespace p64::web::ws {
namespace {

constexpr const char *TAG = "ws";
constexpr int kPeriodMs = 2000;

std::mutex g_mutex;
std::vector<int> g_clients;  // socket descriptors of open WebSocket connections
TaskHandle_t g_task = nullptr;
volatile bool g_wake = false;

esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    const int fd = httpd_req_to_sockfd(req);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.push_back(fd);
    ESP_LOGI(TAG, "client %d connected (%u total)", fd, static_cast<unsigned>(g_clients.size()));
    g_wake = true;
    return ESP_OK;
  }
  // Frames from the client: read and ignore (pings are answered by the server itself).
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
  if (err != ESP_OK) return err;
  if (frame.len > 0 && frame.len < 512) {
    std::vector<uint8_t> buf(frame.len + 1, 0);
    frame.payload = buf.data();
    err = httpd_ws_recv_frame(req, &frame, frame.len);
  }
  if (frame.type == HTTPD_WS_TYPE_CLOSE) {
    const int fd = httpd_req_to_sockfd(req);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), fd), g_clients.end());
  }
  return err;
}

void broadcast(const std::string &text) {
  std::vector<int> clients;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    clients = g_clients;
  }
  httpd_handle_t server = net::http::handle();
  if (!server) return;
  for (int fd : clients) {
    if (httpd_ws_get_fd_info(server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), fd), g_clients.end());
      continue;
    }
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(text.data()));
    frame.len = text.size();
    // Synchronous send from this task: the server serialises access to the socket.
    if (httpd_ws_send_data(server, fd, &frame) != ESP_OK) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), fd), g_clients.end());
    }
  }
}

void push_task(void *) {
  int waited_ms = 0;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(100));
    waited_ms += 100;
    if (!g_wake && waited_ms < kPeriodMs) continue;
    g_wake = false;
    waited_ms = 0;
    bool any;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      any = !g_clients.empty();
    }
    if (!any) continue;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddItemToObject(root, "data", build_status());
    char *text = cJSON_PrintUnformatted(root);
    if (text) broadcast(text);
    cJSON_free(text);
    cJSON_Delete(root);
  }
}

}  // namespace

void register_routes() {
  httpd_uri_t route = {};
  route.uri = "/api/v1/ws";
  route.method = HTTP_GET;
  route.handler = ws_handler;
  route.is_websocket = true;
  route.handle_ws_control_frames = false;
  net::http::add(route);
}

void start() {
  if (g_task) return;
  xTaskCreatePinnedToCoreWithCaps(push_task, "ws_push", 6144, nullptr, 4, &g_task, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void notify() { g_wake = true; }

}  // namespace p64::web::ws

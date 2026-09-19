#include "p64/net/dns_hijack.hpp"

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace p64::net::dns_hijack {
namespace {

constexpr const char *TAG = "dns";
constexpr uint32_t kApAddress = 0xC0A80401;  // 192.168.4.1

std::atomic<bool> g_run{false};
TaskHandle_t g_task = nullptr;

// Minimal DNS responder: copies the query, sets the response flags and appends one A
// record pointing at the access point for whatever name was asked.
void task(void *) {
  const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket failed");
    g_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind failed");
    close(sock);
    g_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  timeval tv = {1, 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  uint8_t buf[512];
  while (g_run.load()) {
    sockaddr_in from = {};
    socklen_t from_len = sizeof(from);
    const int len = recvfrom(sock, buf, sizeof(buf) - 16, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
    if (len < 12) continue;
    // Walk the question name to find its end.
    int pos = 12;
    while (pos < len && buf[pos] != 0) pos += buf[pos] + 1;
    pos += 5;  // terminator + QTYPE + QCLASS
    if (pos > len) continue;
    buf[2] = 0x81;  // response, recursion desired
    buf[3] = 0x80;  // recursion available, no error
    buf[6] = 0;     // one answer
    buf[7] = 1;
    buf[8] = buf[9] = buf[10] = buf[11] = 0;
    int out = pos;
    const uint8_t answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04,
                              static_cast<uint8_t>(kApAddress >> 24), static_cast<uint8_t>(kApAddress >> 16),
                              static_cast<uint8_t>(kApAddress >> 8), static_cast<uint8_t>(kApAddress)};
    std::memcpy(buf + out, answer, sizeof(answer));
    out += sizeof(answer);
    sendto(sock, buf, out, 0, reinterpret_cast<sockaddr *>(&from), from_len);
  }
  close(sock);
  g_task = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

void start() {
  if (g_task) return;
  g_run = true;
  xTaskCreatePinnedToCore(task, "dns", 3072, nullptr, 4, &g_task, 0);
  ESP_LOGI(TAG, "captive DNS answering every name with 192.168.4.1");
}

void stop() {
  g_run = false;  // the task exits on its next receive timeout
}

}  // namespace p64::net::dns_hijack

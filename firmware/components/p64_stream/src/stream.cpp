#include "p64/stream/stream.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "p64/content/psram.hpp"
#include "p64/gfx/scaler.hpp"
#include "p64/system/event_bus.hpp"
#include "p64/system/settings.hpp"
#include "protocol.hpp"

namespace p64::stream {
namespace {

constexpr const char *TAG = "stream";
constexpr int64_t kSecond = 1000000;
constexpr size_t kPacketBytes = 1500 + 64;
constexpr uint32_t kWaitStepMs = 10;

// Shared between the listener task (core 0) and the player task (core 1, in next_frame).
std::mutex g_mutex;
Status g_status;
gfx::Frame *g_latest = nullptr;  // PSRAM: the latest complete frame, scaled to the panel
uint32_t g_latest_serial = 0;    // bumps per complete frame
int64_t g_latest_arrived_us = 0;
bool g_active = false;
int64_t g_fps_window_us = 0;
uint32_t g_fps_count = 0;

SemaphoreHandle_t g_frame_sem = nullptr;  // given per complete frame and per wake()
std::atomic<bool> g_wake{false};
TaskHandle_t g_task = nullptr;
esp_timer_handle_t g_silence = nullptr;
std::shared_ptr<playback::FrameSource> g_source;

// Listener-task state.
int g_ddp_socket = -1, g_raw_socket = -1;
uint16_t g_ddp_port = 0, g_raw_port = 0;
std::atomic<bool> g_reopen{true};
uint8_t *g_ddp_storage = nullptr;  // PSRAM: assembler buffers and the RGB888 canvas
uint8_t *g_raw_storage = nullptr;
uint8_t *g_canvas = nullptr;
Assembler *g_ddp_asm = nullptr;
Assembler *g_raw_asm = nullptr;
RawHeader g_raw_head = {};
bool g_raw_seen = false;
gfx::Frame *g_scratch = nullptr;
gfx::Scaler *g_scaler = nullptr;
int g_scaler_w = 0, g_scaler_h = 0;

void count_rejected() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_status.rejected;
}

void count_incomplete() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_status.incomplete;
}

void arm_silence() {
  const uint32_t ms = system::settings_view()->stream_silence_ms;
  esp_timer_stop(g_silence);
  esp_timer_start_once(g_silence, static_cast<uint64_t>(ms) * 1000);
}

void on_silence(void *) {
  bool ended = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_active) {
      g_active = false;
      g_status.active = false;
      g_status.fps = 0;
      ended = true;
    }
  }
  if (ended) {
    ESP_LOGI(TAG, "stream ended: no frame for %lu ms", static_cast<unsigned long>(system::settings_view()->stream_silence_ms));
    system::publish(system::Event::StreamEnded);
  }
}

// A complete frame: convert, scale into the panel frame, publish to the source.
void deliver(const char *protocol, Format format, int width, int height, bool palette, const uint8_t *data,
             size_t len, const char *sender) {
  if (!to_rgb888(format, width, height, palette, data, len, g_canvas, kMaxPixelBytes)) {
    count_rejected();
    return;
  }
  if (width != g_scaler_w || height != g_scaler_h) {
    g_scaler->configure(width, height, gfx::Frame::width(), gfx::Frame::height());
    g_scaler_w = width;
    g_scaler_h = height;
  }
  g_scaler->scale(g_canvas, *g_scratch, system::settings_view()->background);
  const int64_t now = esp_timer_get_time();
  bool started = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_latest->copy_from(*g_scratch);
    ++g_latest_serial;
    g_latest_arrived_us = now;
    ++g_status.frames;
    g_status.protocol = protocol;
    g_status.width = width;
    g_status.height = height;
    g_status.sender = sender;
    ++g_fps_count;
    if (now - g_fps_window_us >= kSecond) {
      g_status.fps = g_fps_window_us ? static_cast<float>(g_fps_count) * kSecond / static_cast<float>(now - g_fps_window_us)
                                     : 0.0f;
      g_fps_window_us = now;
      g_fps_count = 0;
    }
    if (!g_active) {
      g_active = true;
      g_status.active = true;
      started = true;
    }
  }
  xSemaphoreGive(g_frame_sem);
  arm_silence();
  if (started) {
    ESP_LOGI(TAG, "stream started: %s %dx%d from %s", protocol, width, height, sender);
    system::publish(system::Event::StreamStarted);
  }
}

void handle_ddp(const uint8_t *data, size_t len, const char *sender) {
  DdpHeader h;
  size_t payload;
  if (!parse_ddp(data, len, h, payload)) {
    count_rejected();
    return;
  }
  // A chunk at offset 0 starts a frame. The frame's size is only known at the push, so
  // the assembler runs at the largest DDP size and the byte count decides at the end.
  if (h.offset == 0) {
    if (g_ddp_asm->active()) count_incomplete();
    g_ddp_asm->begin(128u * 128 * 3);
  }
  if (!g_ddp_asm->add(h.offset, data + payload, h.length)) {
    count_rejected();
    return;
  }
  if (!h.push) return;
  const uint32_t total = h.offset + h.length;
  const int side = ddp_side_for_bytes(total);
  if (side == 0 || g_ddp_asm->received() < total) {
    count_incomplete();
    g_ddp_asm->reset();
    return;
  }
  deliver("ddp", Format::Rgb888, side, side, false, g_ddp_asm->data(), total, sender);
  g_ddp_asm->reset();
}

void handle_raw(const uint8_t *data, size_t len, const char *sender) {
  RawHeader h;
  size_t payload;
  if (!parse_raw(data, len, h, payload)) {
    count_rejected();
    return;
  }
  const bool same = g_raw_asm->active() && h.sequence == g_raw_head.sequence && h.total == g_raw_asm->total() &&
                    h.width == g_raw_head.width && h.height == g_raw_head.height && h.format == g_raw_head.format;
  if (!same) {
    if (g_raw_asm->active()) count_incomplete();
    if (g_raw_seen) {
      const uint16_t expected = static_cast<uint16_t>(g_raw_head.sequence + 1);
      if (h.sequence != expected) {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_status.lost;
      }
    }
    g_raw_seen = true;
    g_raw_asm->begin(h.total);
    g_raw_head = h;
  }
  if (!g_raw_asm->add(h.offset, data + payload, len - payload)) {
    count_rejected();
    return;
  }
  if (!g_raw_asm->complete()) return;  // chunks come in any order; the next sequence abandons holes
  deliver("raw", h.format, h.width, h.height, h.palette, g_raw_asm->data(), g_raw_asm->total(), sender);
  g_raw_asm->reset();
}

int open_socket(uint16_t port) {
  const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s < 0) return -1;
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int reuse = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ESP_LOGW(TAG, "bind UDP %u failed: errno %d", port, errno);
    close(s);
    return -1;
  }
  return s;
}

void reopen_sockets() {
  const system::Settings s = system::settings();
  if (g_ddp_socket >= 0) close(g_ddp_socket);
  if (g_raw_socket >= 0) close(g_raw_socket);
  g_ddp_socket = s.ddp_enabled ? open_socket(s.ddp_port) : -1;
  g_raw_socket = s.raw_udp_enabled ? open_socket(s.raw_udp_port) : -1;
  g_ddp_port = s.ddp_port;
  g_raw_port = s.raw_udp_port;
  g_ddp_asm->reset();
  g_raw_asm->reset();
  g_raw_seen = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.ddp_listening = g_ddp_socket >= 0;
    g_status.raw_listening = g_raw_socket >= 0;
  }
  ESP_LOGI(TAG, "listening: DDP %s (UDP %u), raw %s (UDP %u)", g_ddp_socket >= 0 ? "on" : "off", s.ddp_port,
           g_raw_socket >= 0 ? "on" : "off", s.raw_udp_port);
  g_reopen = false;
}

void task(void *) {
  auto *packet = static_cast<uint8_t *>(heap_caps_malloc(kPacketBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  esp_task_wdt_add(nullptr);  // select() waits at most 250 ms
  while (true) {
    esp_task_wdt_reset();
    if (g_reopen) reopen_sockets();
    fd_set set;
    FD_ZERO(&set);
    int maxfd = -1;
    for (int s : {g_ddp_socket, g_raw_socket}) {
      if (s < 0) continue;
      FD_SET(s, &set);
      if (s > maxfd) maxfd = s;
    }
    if (maxfd < 0) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    timeval tv = {0, 250000};
    const int ready = select(maxfd + 1, &set, nullptr, nullptr, &tv);
    if (ready <= 0) continue;
    for (int s : {g_ddp_socket, g_raw_socket}) {
      if (s < 0 || !FD_ISSET(s, &set)) continue;
      sockaddr_in from = {};
      socklen_t from_len = sizeof(from);
      const int n = recvfrom(s, packet, kPacketBytes, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
      if (n <= 0) continue;
      char sender[24];
      inet_ntoa_r(from.sin_addr, sender, sizeof(sender));
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_status.datagrams;
      }
      if (s == g_ddp_socket) {
        handle_ddp(packet, static_cast<size_t>(n), sender);
      } else {
        handle_raw(packet, static_cast<size_t>(n), sender);
      }
    }
  }
}

// Hands the player the freshest frame at each slot: when the player asks for a frame
// due in the future (it works one 60 fps slot ahead), the source waits until just
// before that instant so a frame landing meanwhile replaces an older one (latest wins,
// no buffering beyond one frame). Without a due time (first frame) or after wake() it
// returns at once.
class StreamSource : public playback::FrameSource {
 public:
  const std::string &name() const override { return name_; }
  bool is_static() const override { return false; }
  bool next_frame(gfx::Frame &out, uint32_t &delay_ms, int64_t due_us) override {
    const int64_t target = due_us ? due_us - 2000 : 0;
    const int64_t deadline = esp_timer_get_time() + kSecond;
    while (true) {
      const int64_t now = esp_timer_get_time();
      const bool woken = g_wake.exchange(false);
      if (now >= target || woken) {
        std::lock_guard<std::mutex> lock(g_mutex);
        const bool fresh = g_latest_serial != served_;
        if (fresh || woken || now >= deadline) {
          served_ = g_latest_serial;
          out.copy_from(*g_latest);
          if (fresh) g_status.last_latency_us = static_cast<uint32_t>(now - g_latest_arrived_us);
          // 0: replaced as soon as the next lands (the player applies the 60 fps cap);
          // after a second without frames the same frame is re-issued at a slow pace so
          // the player keeps polling until the show swaps the stream out.
          delay_ms = fresh || woken ? 0 : 200;
          return true;
        }
      }
      const int64_t wait_us = target > now ? target - now : static_cast<int64_t>(kWaitStepMs) * 1000;
      uint32_t wait_ms = static_cast<uint32_t>(wait_us / 1000);
      if (wait_ms > kWaitStepMs) wait_ms = kWaitStepMs;
      if (wait_ms < 1) wait_ms = 1;
      xSemaphoreTake(g_frame_sem, pdMS_TO_TICKS(wait_ms));
    }
  }

 private:
  std::string name_ = "stream";
  uint32_t served_ = 0;
};

}  // namespace

bool start() {
  if (g_task) return true;
  auto *frames = static_cast<gfx::Frame *>(heap_caps_malloc(sizeof(gfx::Frame) * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frames) return false;
  g_latest = new (&frames[0]) gfx::Frame();
  g_scratch = new (&frames[1]) gfx::Frame();
  g_ddp_storage = static_cast<uint8_t *>(heap_caps_malloc(kMaxFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_raw_storage = static_cast<uint8_t *>(heap_caps_malloc(kMaxFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_canvas = static_cast<uint8_t *>(heap_caps_malloc(kMaxPixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_ddp_storage || !g_raw_storage || !g_canvas) return false;
  g_ddp_asm = new Assembler(g_ddp_storage, kMaxFrameBytes);
  g_raw_asm = new Assembler(g_raw_storage, kMaxFrameBytes);
  g_scaler = new gfx::Scaler();
  g_frame_sem = xSemaphoreCreateBinary();
  g_source = std::allocate_shared<StreamSource>(content::PsramAllocator<StreamSource>());
  const esp_timer_create_args_t args = {on_silence, nullptr, ESP_TIMER_TASK, "stream_silence", false};
  esp_timer_create(&args, &g_silence);
  system::subscribe(system::Event::SettingsChanged, [](const system::Message &) {
    const system::Settings s = system::settings();
    if (s.ddp_port != g_ddp_port || s.raw_udp_port != g_raw_port || s.ddp_enabled != (g_ddp_socket >= 0) ||
        s.raw_udp_enabled != (g_raw_socket >= 0)) {
      g_reopen = true;
    }
  });
  // Sockets, assembly and scaling only (no flash writes): the stack can live in PSRAM.
  const BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(task, "stream", 8192, nullptr, 9, &g_task, 0,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ok == pdPASS;
}

std::shared_ptr<playback::FrameSource> source() { return g_source; }

bool active() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_active;
}

void wake() {
  g_wake = true;
  if (g_frame_sem) xSemaphoreGive(g_frame_sem);
}

Status status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}

cJSON *status_json() {
  const Status s = status();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "active", s.active);
  cJSON_AddStringToObject(o, "protocol", s.protocol.c_str());
  cJSON_AddNumberToObject(o, "width", s.width);
  cJSON_AddNumberToObject(o, "height", s.height);
  cJSON_AddNumberToObject(o, "frames", s.frames);
  cJSON_AddNumberToObject(o, "incomplete", s.incomplete);
  cJSON_AddNumberToObject(o, "rejected", s.rejected);
  cJSON_AddNumberToObject(o, "lost", s.lost);
  cJSON_AddNumberToObject(o, "datagrams", s.datagrams);
  cJSON_AddNumberToObject(o, "fps", s.fps);
  cJSON_AddNumberToObject(o, "last_latency_ms", s.last_latency_us / 1000.0);
  cJSON_AddStringToObject(o, "sender", s.sender.c_str());
  cJSON_AddBoolToObject(o, "ddp_listening", s.ddp_listening);
  cJSON_AddBoolToObject(o, "raw_listening", s.raw_listening);
  return o;
}

}  // namespace p64::stream

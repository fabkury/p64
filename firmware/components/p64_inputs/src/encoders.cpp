#include "encoders.hpp"

#include <cstdint>
#include <mutex>

#include "encoder_model.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "p64/system/i2c_bus.hpp"
#include "sdkconfig.h"
#include "seesaw.hpp"

namespace p64::inputs::encoders {
namespace {

constexpr const char *TAG = "encoders";
constexpr uint32_t kPollMs = 20;                       // 50 Hz; the seesaw counts detents between polls
constexpr uint32_t kRetryPolls = 5000 / kPollMs;       // a missing board is tried again every 5 s
constexpr uint32_t kIdentifyPolls = 2000 / kPollMs;    // the NeoPixel identify at start
constexpr uint32_t kLostAfterErrors = 10;              // consecutive read failures before a board counts as gone

struct Knob {
  Knob(const char *n, uint8_t a, uint8_t r_, uint8_t g_, uint8_t b_) : name(n), address(a), r(r_), g(g_), b(b_) {}
  const char *name;
  uint8_t address;
  uint8_t r, g, b;  // the identify colour
  seesaw::Board board;
  EncoderTracker tracker;
  uint32_t consecutive_errors = 0;
  uint32_t read_errors = 0;
  uint32_t polls = 0;
  uint32_t lost = 0;  // how many times the board stopped answering
  bool warned_missing = false;
  int64_t last_event_us = 0;
  const char *last_event = "";
  uint32_t pixel_until_poll = 0;  // the identify colour stays on until this poll
};

#ifdef CONFIG_P64_ENCODERS
Knob g_knobs[2] = {{"A", CONFIG_P64_ENCODER_A_ADDRESS, 0, 40, 0}, {"B", CONFIG_P64_ENCODER_B_ADDRESS, 0, 0, 40}};
#else
Knob g_knobs[2] = {{"A", 0x36, 0, 40, 0}, {"B", 0x37, 0, 0, 40}};  // never opened
#endif
std::mutex g_mutex;
bool g_started = false;
uint32_t g_poll = 0;

// Opens a board that is not open and lights its identify colour; true when it answers.
// The probe first, so an absent board costs one quiet NACK and not the driver's error
// line every 5 s on a p64a.
bool try_open(Knob &k, i2c_master_bus_handle_t bus, uint32_t poll) {
  if (i2c_master_probe(bus, k.address, 20) != ESP_OK || !k.board.open(bus, k.address)) {
    if (!k.warned_missing) {
      ESP_LOGW(TAG, "knob %s: no seesaw at 0x%02x (polled again every 5 s)", k.name, k.address);
      k.warned_missing = true;
    }
    return false;
  }
  k.warned_missing = false;
  k.consecutive_errors = 0;
  k.tracker.resync();
  ESP_LOGI(TAG, "knob %s: seesaw at 0x%02x, hardware id 0x%02x", k.name, k.address, k.board.hw_id());
  k.board.set_pixel(k.r, k.g, k.b);
  k.pixel_until_poll = poll + kIdentifyPolls;
  return true;
}

void poll_knob(Knob &k, uint32_t t_ms) {
  int32_t position = 0;
  bool down = false;
  if (!k.board.read_position(position) || !k.board.read_switch(down)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++k.read_errors;
    if (++k.consecutive_errors == kLostAfterErrors) {
      ++k.lost;
      k.board.close();
      k.warned_missing = false;
      ESP_LOGW(TAG, "knob %s: 0x%02x stopped answering (%lu read errors)", k.name, k.address,
               static_cast<unsigned long>(k.read_errors));
    }
    return;
  }
  EncoderEvent ev;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    k.consecutive_errors = 0;
    ++k.polls;
    ev = k.tracker.feed(t_ms, position, down);
    if (ev.any()) {
      k.last_event_us = esp_timer_get_time();
      k.last_event = ev.turned > 0 ? "cw" : ev.turned < 0 ? "ccw" : ev.long_press ? "long" : ev.pressed ? "press" : "release";
    }
  }
  // Stage B: log only. Stage C hands these to the show loop.
  if (ev.turned != 0)
    ESP_LOGI(TAG, "knob %s: %+ld (position %ld)", k.name, static_cast<long>(ev.turned), static_cast<long>(position));
  if (ev.pressed) ESP_LOGI(TAG, "knob %s: pressed", k.name);
  if (ev.long_press) ESP_LOGI(TAG, "knob %s: long press", k.name);
  if (ev.released) ESP_LOGI(TAG, "knob %s: released", k.name);
}

void task(void *) {
  i2c_master_bus_handle_t bus = system::i2c_ext_bus();
  if (!bus) {
    ESP_LOGE(TAG, "no external I2C bus: encoders off");
    vTaskDelete(nullptr);
    return;
  }
  for (Knob &k : g_knobs) try_open(k, bus, 0);
  TickType_t wake = xTaskGetTickCount();
  while (true) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPollMs));
    const uint32_t poll = ++g_poll;
    const auto t_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    for (Knob &k : g_knobs) {
      if (!k.board.is_open()) {
        if (poll % kRetryPolls == 0) try_open(k, bus, poll);
        continue;
      }
      if (k.pixel_until_poll && poll >= k.pixel_until_poll) {
        k.board.set_pixel(0, 0, 0);
        k.pixel_until_poll = 0;
      }
      poll_knob(k, t_ms);
    }
  }
}

}  // namespace

void start() {
#ifdef CONFIG_P64_ENCODERS
  if (g_started) return;
  g_started = true;
  ESP_LOGI(TAG, "external I2C on SDA %d / SCL %d, knob A 0x%02x, knob B 0x%02x", CONFIG_P64_I2C_EXT_SDA,
           CONFIG_P64_I2C_EXT_SCL, CONFIG_P64_ENCODER_A_ADDRESS, CONFIG_P64_ENCODER_B_ADDRESS);
  // I2C reads and arithmetic only, never flash: the stack lives in PSRAM.
  xTaskCreatePinnedToCoreWithCaps(task, "encoders", 4096, nullptr, 5, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

int present() {
  std::lock_guard<std::mutex> lock(g_mutex);
  int n = 0;
  for (const Knob &k : g_knobs) n += k.board.is_open() ? 1 : 0;
  return n;
}

cJSON *json(bool scan) {
  cJSON *d = cJSON_CreateObject();
#ifdef CONFIG_P64_ENCODERS
  cJSON_AddBoolToObject(d, "enabled", true);
  cJSON_AddNumberToObject(d, "sda", CONFIG_P64_I2C_EXT_SDA);
  cJSON_AddNumberToObject(d, "scl", CONFIG_P64_I2C_EXT_SCL);
  // The idle levels of the two lines (both 1 when the bus is free): a board or a cable
  // holding one low shows here, which a scan alone cannot tell from an empty bus.
  cJSON_AddNumberToObject(d, "sda_level", gpio_get_level(static_cast<gpio_num_t>(CONFIG_P64_I2C_EXT_SDA)));
  cJSON_AddNumberToObject(d, "scl_level", gpio_get_level(static_cast<gpio_num_t>(CONFIG_P64_I2C_EXT_SCL)));
#else
  cJSON_AddBoolToObject(d, "enabled", false);
#endif
  cJSON_AddNumberToObject(d, "polls", g_poll);
  if (scan) {
    cJSON *found = cJSON_AddArrayToObject(d, "scan");
    i2c_master_bus_handle_t bus = system::i2c_ext_bus();
    for (uint16_t a = 0x08; bus && a <= 0x77; ++a) {
      if (i2c_master_probe(bus, a, 5) == ESP_OK) cJSON_AddItemToArray(found, cJSON_CreateNumber(a));
    }
  }
  cJSON *knobs = cJSON_AddArrayToObject(d, "knobs");
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const Knob &k : g_knobs) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", k.name);
    cJSON_AddNumberToObject(j, "address", k.address);
    cJSON_AddBoolToObject(j, "present", k.board.is_open());
    cJSON_AddNumberToObject(j, "hw_id", k.board.hw_id());
    cJSON_AddNumberToObject(j, "position", k.tracker.position());
    cJSON_AddNumberToObject(j, "detents", static_cast<double>(k.tracker.detents()));
    cJSON_AddBoolToObject(j, "pressed", k.tracker.held());
    cJSON_AddNumberToObject(j, "presses", k.tracker.presses());
    cJSON_AddNumberToObject(j, "long_presses", k.tracker.long_presses());
    cJSON_AddNumberToObject(j, "polls", k.polls);
    cJSON_AddNumberToObject(j, "read_errors", k.read_errors);
    cJSON_AddNumberToObject(j, "lost", k.lost);
    cJSON_AddStringToObject(j, "last_event", k.last_event);
    cJSON_AddNumberToObject(j, "last_event_age_s", k.last_event_us ? (esp_timer_get_time() - k.last_event_us) / 1e6 : -1);
    cJSON_AddItemToArray(knobs, j);
  }
  return d;
}

}  // namespace p64::inputs::encoders

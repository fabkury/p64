#include "shtc3.hpp"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace p64::widgets::shtc3 {
namespace {

constexpr const char *TAG = "shtc3";
constexpr uint16_t kAddress = 0x70;
constexpr uint8_t kWakeup[] = {0x35, 0x17};
constexpr uint8_t kSleep[] = {0xB0, 0x98};
constexpr uint8_t kMeasure[] = {0x7C, 0xA2};  // clock stretching, normal mode, temperature first

i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_dev = nullptr;

uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

}  // namespace

bool init() {
  if (g_dev) return true;
  i2c_master_bus_config_t bus = {};
  bus.i2c_port = I2C_NUM_0;
  bus.sda_io_num = static_cast<gpio_num_t>(CONFIG_P64_I2C_SDA);
  bus.scl_io_num = static_cast<gpio_num_t>(CONFIG_P64_I2C_SCL);
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.flags.enable_internal_pullup = true;
  esp_err_t err = i2c_new_master_bus(&bus, &g_bus);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "I2C bus: %s", esp_err_to_name(err));
    return false;
  }
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = kAddress;
  dev.scl_speed_hz = 100000;
  dev.scl_wait_us = 20000;  // the measurement stretches the clock for up to 12 ms
  err = i2c_master_bus_add_device(g_bus, &dev, &g_dev);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "I2C device: %s", esp_err_to_name(err));
    return false;
  }
  float t, h;
  if (!read(t, h)) {
    ESP_LOGW(TAG, "no SHTC3 answer on SDA %d / SCL %d", CONFIG_P64_I2C_SDA, CONFIG_P64_I2C_SCL);
    return false;
  }
  ESP_LOGI(TAG, "SHTC3 ready: %.1f C, %.0f %% RH", static_cast<double>(t), static_cast<double>(h));
  return true;
}

bool read(float &temperature_c, float &humidity) {
  if (!g_dev) return false;
  if (i2c_master_transmit(g_dev, kWakeup, sizeof(kWakeup), 50) != ESP_OK) return false;
  vTaskDelay(pdMS_TO_TICKS(1));
  uint8_t raw[6] = {};
  const esp_err_t err = i2c_master_transmit_receive(g_dev, kMeasure, sizeof(kMeasure), raw, sizeof(raw), 100);
  i2c_master_transmit(g_dev, kSleep, sizeof(kSleep), 50);
  if (err != ESP_OK) return false;
  if (crc8(raw, 2) != raw[2] || crc8(raw + 3, 2) != raw[5]) return false;
  const uint16_t t = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
  const uint16_t h = static_cast<uint16_t>((raw[3] << 8) | raw[4]);
  temperature_c = -45.0f + 175.0f * static_cast<float>(t) / 65536.0f;
  humidity = 100.0f * static_cast<float>(h) / 65536.0f;
  return true;
}

}  // namespace p64::widgets::shtc3

#include "p64/system/i2c_bus.hpp"

#include <mutex>

#include "esp_log.h"
#include "sdkconfig.h"

namespace p64::system {
namespace {

constexpr const char *TAG = "i2c";
std::mutex g_mutex;
i2c_master_bus_handle_t g_bus = nullptr;
bool g_tried = false;

}  // namespace

i2c_master_bus_handle_t i2c_bus() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_bus || g_tried) return g_bus;
  g_tried = true;
  i2c_master_bus_config_t bus = {};
  bus.i2c_port = I2C_NUM_0;
  bus.sda_io_num = static_cast<gpio_num_t>(CONFIG_P64_I2C_SDA);
  bus.scl_io_num = static_cast<gpio_num_t>(CONFIG_P64_I2C_SCL);
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.flags.enable_internal_pullup = true;
  const esp_err_t err = i2c_new_master_bus(&bus, &g_bus);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "bus on SDA %d / SCL %d: %s", CONFIG_P64_I2C_SDA, CONFIG_P64_I2C_SCL, esp_err_to_name(err));
    g_bus = nullptr;
  }
  return g_bus;
}

i2c_master_dev_handle_t i2c_add_device(uint16_t address, uint32_t speed_hz, uint32_t scl_wait_us) {
  i2c_master_bus_handle_t bus = i2c_bus();
  if (!bus) return nullptr;
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = address;
  dev.scl_speed_hz = speed_hz;
  dev.scl_wait_us = scl_wait_us;
  i2c_master_dev_handle_t handle = nullptr;
  const esp_err_t err = i2c_master_bus_add_device(bus, &dev, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "device 0x%02x: %s", address, esp_err_to_name(err));
    return nullptr;
  }
  return handle;
}

}  // namespace p64::system

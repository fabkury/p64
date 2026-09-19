#include "qmi8658.hpp"

#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p64/system/i2c_bus.hpp"

namespace p64::inputs::qmi8658 {
namespace {

constexpr const char *TAG = "qmi8658";
constexpr uint8_t kWhoAmI = 0x00, kRevision = 0x01, kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl3 = 0x04, kCtrl5 = 0x06,
                  kCtrl7 = 0x08, kStatus0 = 0x2E, kAxL = 0x35, kReset = 0x60;
constexpr uint8_t kChipId = 0x05;
constexpr float kLsbPerG = 4096.0f;  // +-8 g full scale, 16-bit

i2c_master_dev_handle_t g_dev = nullptr;

bool write_reg(uint8_t reg, uint8_t value) {
  const uint8_t buf[2] = {reg, value};
  return i2c_master_transmit(g_dev, buf, sizeof(buf), 50) == ESP_OK;
}

bool read_regs(uint8_t reg, uint8_t *out, size_t len) {
  return i2c_master_transmit_receive(g_dev, &reg, 1, out, len, 50) == ESP_OK;
}

bool probe(uint16_t address) {
  i2c_master_dev_handle_t dev = system::i2c_add_device(address, 400000);
  if (!dev) return false;
  g_dev = dev;
  uint8_t id = 0;
  if (read_regs(kWhoAmI, &id, 1) && id == kChipId) return true;
  i2c_master_bus_rm_device(dev);
  g_dev = nullptr;
  return false;
}

}  // namespace

bool init() {
  if (g_dev) return true;
  if (!probe(0x6B) && !probe(0x6A)) {
    ESP_LOGW(TAG, "no QMI8658 on the I2C bus");
    return false;
  }
  write_reg(kReset, 0xB0);
  vTaskDelay(pdMS_TO_TICKS(20));
  uint8_t rev = 0;
  read_regs(kRevision, &rev, 1);
  bool ok = write_reg(kCtrl1, 0x40);   // register address auto-increment, little-endian data
  ok = write_reg(kCtrl2, 0x24) && ok;  // accelerometer +-8 g (010), 500 Hz (0100)
  ok = write_reg(kCtrl3, 0x00) && ok;  // gyroscope off
  ok = write_reg(kCtrl5, 0x00) && ok;  // no low-pass filter: taps are short impulses
  ok = write_reg(kCtrl7, 0x01) && ok;  // accelerometer on
  vTaskDelay(pdMS_TO_TICKS(10));
  float ax, ay, az;
  if (!ok || !read(ax, ay, az)) {
    ESP_LOGW(TAG, "QMI8658 found but not answering samples");
    return false;
  }
  ESP_LOGI(TAG, "QMI8658 ready (revision 0x%02x): %.2f %.2f %.2f g", rev, static_cast<double>(ax), static_cast<double>(ay),
           static_cast<double>(az));
  return true;
}

bool present() { return g_dev != nullptr; }

bool read(float &ax, float &ay, float &az) {
  if (!g_dev) return false;
  uint8_t raw[6];
  if (!read_regs(kAxL, raw, sizeof(raw))) return false;
  const auto x = static_cast<int16_t>(raw[0] | (raw[1] << 8));
  const auto y = static_cast<int16_t>(raw[2] | (raw[3] << 8));
  const auto z = static_cast<int16_t>(raw[4] | (raw[5] << 8));
  ax = static_cast<float>(x) / kLsbPerG;
  ay = static_cast<float>(y) / kLsbPerG;
  az = static_cast<float>(z) / kLsbPerG;
  return true;
}

}  // namespace p64::inputs::qmi8658

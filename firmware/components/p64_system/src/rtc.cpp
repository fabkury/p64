#include "p64/system/rtc.hpp"

#include <mutex>

#include "esp_log.h"
#include "p64/system/i2c_bus.hpp"
#include "p64/system/rtc_codec.hpp"

namespace p64::system::rtc {
namespace {

constexpr const char *TAG = "rtc";
constexpr uint16_t kAddress = 0x51;
constexpr uint8_t kControl1 = 0x00;

std::mutex g_mutex;
i2c_master_dev_handle_t g_dev = nullptr;
bool g_tried = false;

bool open() {
  if (g_dev || g_tried) return g_dev != nullptr;
  g_tried = true;
  i2c_master_dev_handle_t dev = i2c_add_device(kAddress, 100000);
  if (!dev) return false;
  // Control_1: 24 h mode, oscillator running, 12.5 pF capacitor (the board's crystal load
  // per Waveshare's driver), no correction interrupt.
  const uint8_t ctrl[2] = {kControl1, 0x00};
  uint8_t probe = 0;
  if (i2c_master_transmit_receive(dev, ctrl, 1, &probe, 1, 50) != ESP_OK) {
    ESP_LOGW(TAG, "no PCF85063A at 0x%02x", kAddress);
    i2c_master_bus_rm_device(dev);
    return false;
  }
  if (probe & 0x20) i2c_master_transmit(dev, ctrl, 2, 50);  // STOP set: start the clock
  g_dev = dev;
  ESP_LOGI(TAG, "PCF85063A ready (control 0x%02x)", probe);
  return true;
}

}  // namespace

bool present() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return open();
}

bool read(time_t &utc) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!open()) return false;
  const uint8_t reg = rtc_codec::kFirstRegister;
  uint8_t regs[rtc_codec::kRegisters] = {};
  if (i2c_master_transmit_receive(g_dev, &reg, 1, regs, sizeof(regs), 50) != ESP_OK) return false;
  return rtc_codec::decode(regs, utc);
}

bool write(time_t utc) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!open()) return false;
  uint8_t buf[1 + rtc_codec::kRegisters] = {rtc_codec::kFirstRegister};
  rtc_codec::encode(utc, buf + 1);
  const esp_err_t err = i2c_master_transmit(g_dev, buf, sizeof(buf), 50);
  if (err != ESP_OK) ESP_LOGW(TAG, "write: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

}  // namespace p64::system::rtc

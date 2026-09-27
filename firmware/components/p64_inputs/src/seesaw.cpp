#include "seesaw.hpp"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "seesaw_wire.hpp"

namespace p64::inputs::seesaw {
namespace {

constexpr const char *TAG = "seesaw";
constexpr uint32_t kSpeedHz = 100000;  // 300 mm of cable: 100 kHz first, 400 kHz once everything else is verified
constexpr int kTimeoutMs = 20;

}  // namespace

bool Board::write(const uint8_t *data, size_t len) {
  return dev_ && i2c_master_transmit(dev_, data, len, kTimeoutMs) == ESP_OK;
}

// A seesaw read is two transactions: the register address with a STOP, a pause the
// firmware on the board needs to fetch the value, then the read. A combined
// write-then-read (repeated start) leaves no time and returns stale bytes.
bool Board::read(uint8_t module, uint8_t reg, uint8_t *out, size_t len) {
  const auto a = reg_address(module, reg);
  if (!write(a.data(), a.size())) return false;
  esp_rom_delay_us(kReadDelayUs);
  return i2c_master_receive(dev_, out, len, kTimeoutMs) == ESP_OK;
}

bool Board::open(i2c_master_bus_handle_t bus, uint8_t address) {
  if (dev_) return true;
  if (!bus) return false;
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = address;
  cfg.scl_speed_hz = kSpeedHz;
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
  dev_ = dev;
  address_ = address;
  // Reset, then let the board come back (Adafruit waits 10 ms and retries the ID read).
  const auto rst = reset_command();
  if (!write(rst.data(), rst.size())) {
    close();
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(10));
  uint8_t id = 0;
  bool ok = false;
  for (int attempt = 0; attempt < 5 && !ok; ++attempt) {
    ok = read(kStatus, kStatusHwId, &id, 1);
    if (!ok) vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!ok) {
    close();
    return false;
  }
  hw_id_ = id;
  if (!known_hw_id(id)) ESP_LOGW(TAG, "0x%02x: unknown seesaw hardware id 0x%02x (expected 0x87)", address, id);
  // The switch: input with the pull-up (direction clear, pull enable, output high).
  const uint32_t mask = pin_mask(kSwitchPin);
  const auto dir = gpio_mask_command(kGpioDirClrBulk, mask);
  const auto pull = gpio_mask_command(kGpioPullEnSet, mask);
  const auto high = gpio_mask_command(kGpioBulkSet, mask);
  ok = write(dir.data(), dir.size()) && write(pull.data(), pull.size()) && write(high.data(), high.size());
  // The NeoPixel: pin 6, 800 kHz, one pixel, off.
  const auto pin = neopixel_pin_command();
  const auto speed = neopixel_speed_command();
  const auto length = neopixel_length_command();
  ok = ok && write(pin.data(), pin.size()) && write(speed.data(), speed.size()) && write(length.data(), length.size());
  ok = ok && set_pixel(0, 0, 0);
  if (!ok) {
    ESP_LOGW(TAG, "0x%02x answered (hw id 0x%02x) but the setup writes failed", address, id);
    close();
    return false;
  }
  return true;
}

void Board::close() {
  if (dev_) i2c_master_bus_rm_device(dev_);
  dev_ = nullptr;
}

bool Board::read_position(int32_t &position) {
  uint8_t raw[4];
  if (!read(kEncoder, kEncoderPosition, raw, sizeof(raw))) return false;
  position = from_be32_signed(raw);
  return true;
}

bool Board::read_switch(bool &down) {
  uint8_t raw[4];
  if (!read(kGpio, kGpioBulk, raw, sizeof(raw))) return false;
  down = switch_pressed(from_be32(raw));
  return true;
}

bool Board::set_pixel(uint8_t r, uint8_t g, uint8_t b) {
  const auto colour = neopixel_colour_command(r, g, b);
  const auto show = neopixel_show_command();
  return write(colour.data(), colour.size()) && write(show.data(), show.size());
}

}  // namespace p64::inputs::seesaw

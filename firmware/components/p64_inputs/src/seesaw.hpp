// p64 -- one Adafruit seesaw rotary encoder board (5880) on the external I2C bus: the
// register traffic (address, STOP, a 250 us pause, read). Everything it sends is built
// by seesaw_wire.hpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"

namespace p64::inputs::seesaw {

class Board {
 public:
  // Adds the device to the bus, resets it, reads its hardware ID and configures the
  // switch and the NeoPixel. False when nothing answers (the device is removed again).
  bool open(i2c_master_bus_handle_t bus, uint8_t address);
  void close();
  bool is_open() const { return dev_ != nullptr; }
  uint8_t address() const { return address_; }
  uint8_t hw_id() const { return hw_id_; }

  bool read_position(int32_t &position);
  bool read_switch(bool &down);
  bool set_pixel(uint8_t r, uint8_t g, uint8_t b);

 private:
  bool write(const uint8_t *data, size_t len);
  bool read(uint8_t module, uint8_t reg, uint8_t *out, size_t len);

  i2c_master_dev_handle_t dev_ = nullptr;
  uint8_t address_ = 0;
  uint8_t hw_id_ = 0;
};

}  // namespace p64::inputs::seesaw

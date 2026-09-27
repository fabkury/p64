// p64 -- the board's I2C buses, created on first use. The internal bus (SHTC3, QMI8658
// IMU, PCF85063A RTC on CONFIG_P64_I2C_SDA / SCL) and, on the controller's 4-pin GPIO
// socket (CONFIG_P64_I2C_EXT_SDA / SCL, IO45 / IO46), the external bus of the p64b
// rotary encoders. The new i2c_master driver serialises the devices' transactions on a
// bus, so every owner just adds its device; two buses never contend.
#pragma once

#include "driver/i2c_master.h"

namespace p64::system {

// Null when the bus could not be created (logged once).
i2c_master_bus_handle_t i2c_bus();
// Adds a 7-bit device at `address` with `speed_hz` to the internal bus; null on failure.
i2c_master_dev_handle_t i2c_add_device(uint16_t address, uint32_t speed_hz, uint32_t scl_wait_us = 0);
// The external bus on the GPIO socket (I2C_NUM_1); null when it could not be created.
i2c_master_bus_handle_t i2c_ext_bus();

}  // namespace p64::system

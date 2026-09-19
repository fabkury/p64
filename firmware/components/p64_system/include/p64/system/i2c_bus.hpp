// p64 -- the board's one internal I2C bus (SHTC3, QMI8658 IMU, PCF85063A RTC on
// CONFIG_P64_I2C_SDA / SCL), created on first use. The new i2c_master driver
// serialises the devices' transactions, so every owner just adds its device.
#pragma once

#include "driver/i2c_master.h"

namespace p64::system {

// Null when the bus could not be created (logged once).
i2c_master_bus_handle_t i2c_bus();
// Adds a 7-bit device at `address` with `speed_hz`; null on failure.
i2c_master_dev_handle_t i2c_add_device(uint16_t address, uint32_t speed_hz, uint32_t scl_wait_us = 0);

}  // namespace p64::system

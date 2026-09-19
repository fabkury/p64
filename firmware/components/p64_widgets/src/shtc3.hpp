// The board's SHTC3 temperature and humidity sensor on the internal I2C bus (SDA 47,
// SCL 48, address 0x70). Wake, measure with clock stretching, sleep.
#pragma once

namespace p64::widgets::shtc3 {

bool init();
// One measurement; false when the sensor does not answer or the checksums fail.
bool read(float &temperature_c, float &humidity);

}  // namespace p64::widgets::shtc3

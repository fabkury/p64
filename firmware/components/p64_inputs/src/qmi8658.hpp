// p64 -- QMI8658 6-axis IMU on the internal I2C bus: accelerometer only, +-8 g at
// 500 Hz, read by polling (no interrupt line is wired to a known pin).
#pragma once

namespace p64::inputs::qmi8658 {

// Probes 0x6B then 0x6A, resets and configures the accelerometer. False when absent.
bool init();
bool present();
// The latest sample in g. False on a bus error.
bool read(float &ax, float &ay, float &az);

}  // namespace p64::inputs::qmi8658

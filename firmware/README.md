# p64 firmware

The firmware of the p64 device: a desktop 64x64 RGB LED matrix built on Waveshare's
ESP32-S3-RGB-Matrix driver board and RGB-Matrix-P2-64x64 panel (the repository README
lists the hardware).

This folder was started from zero on 2026-09-19. Nothing is decided yet: the device's
features and the firmware's architecture are the next discussion, and this README is the
only file until then.

## Relation to `hardware-tests/`

`hardware-tests/` is the previous `firmware/` folder: the test firmware that brought up
the driver board and the panel (git history follows the rename; `git log --follow` works
on its files). It is the technical reference for this project, not the architectural one:

- Take from it what the hardware taught: the pin map, the vendored and patched esp-hub75
  driver, the frame pacing against the panel's DMA, the tonal-depth and refresh numbers,
  the GDMA priority lesson, the network settings and measured throughput, the microSD
  wiring, the ESP-IDF tool scripts and environment facts.
- Do not take its architecture or code patterns (the scene loop, the module layout, the
  fetcher/web split) as given; they were shaped by testing one thing at a time, not by
  the device we want to build.

Its README, `hardware-tests/README.md`, holds the details and the measured numbers.

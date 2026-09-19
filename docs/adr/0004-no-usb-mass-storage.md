---
status: accepted
date: 2026-09-19
---

# No USB mass storage; the web UI manages the card

p3a exposes its microSD card over USB so owners drag files onto it. p64 does not: the
ESP32-S3's USB-OTG mass-storage device and its USB Serial/JTAG console share the same
pins and cannot both be on, and the console is the only way to flash and read logs from a
device in a sealed shell. Files reach the card through the web UI's file manager (browse,
upload, create folder, rename, delete, play now), which p3a lacks. USB mass storage may
be revisited if a UART header becomes available in a later shell.

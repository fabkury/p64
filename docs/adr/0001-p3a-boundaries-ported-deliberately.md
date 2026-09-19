---
status: accepted
date: 2026-09-19
---

# Adopt p3a's module boundaries; port its components deliberately, not wholesale

p3a is the user's production ESP32-P4 pixel-art player: the same author, the same
Makapix Club contract, a web UI the user wants replicated, and two years of field lessons.
p64 keeps p3a's module boundaries and contracts (state machine, config store, event bus,
play scheduler, channel manager, Makapix client, Wi-Fi manager, HTTP API, OTA manager) so
the web UI and the Makapix integration port with minimal thought, but each component is
ported on its own merits and reimplemented where the ESP32-S3 (about 350 KB internal RAM,
16 MB PSRAM, 240 MHz Xtensa, 64x64 output) makes p3a's internals (sized for 720x720 and
32 MB PSRAM) the wrong shape. The hardware tests in `hardware-tests/` are a technical
reference only: their scene loop and module layout are not carried over.

## Considered options

- Port p3a as-is, replacing the display driver: fastest to parity, but carries P4-sized
  buffers, a P4 task layout and p3a's slow-decode behaviour into a device with a third of
  the RAM.
- Design fresh with p3a as a code quarry: loses the contract compatibility that makes the
  web UI and Makapix work trivial, and re-learns p3a's lessons.

---
status: accepted
date: 2026-09-19
---

# Full Makapix Club pairing (mutual TLS and MQTT) ships in v1, with a fixed TLS budget

p64 pairs with Makapix Club the way p3a does: a provisioning code shown on the panel, a
per-device certificate, a bearer token, a persistent MQTT session over mutual TLS for
commands and presence, and token-based certificate renewal. The anonymous path (public
listings and the promoted feed) is kept only for the Promoted channel before pairing.

Why: every Makapix channel kind but Promoted, "send to device", Likes and view
attribution require pairing, and the server plans daily per-IP caps on its anonymous
endpoints, so an unpaired device would be a second-class player. The cost on the
ESP32-S3 is internal RAM: the hardware tests ran out with two concurrent TLS sessions
next to the panel buffers. The architecture therefore fixes a budget up front: one
persistent MQTT TLS session, at most one download TLS session at a time, TLS buffers
allocated from PSRAM with dynamic sizing (the measured throughput cost, 240 to 390 KB/s
from the CDN, is acceptable), and the budget is verified on hardware before pairing is
declared shipped.

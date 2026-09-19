---
status: accepted
date: 2026-09-19
---

# Decode live, never skip a frame; slow down when decoding is late

Animations are decoded frame by frame from the file while they play; the device never
caches decoded frames in PSRAM, and it never skips a frame to stay on the wall clock.
When a frame decodes later than the previous frame's delay, the previous frame stays up
and the timeline re-anchors from the late frame. Late frames are counted and visible.
The guaranteed envelope is 64x64 at 60 fps; 128x128 at 60 fps is best effort.

Why: the user weighs showing every frame of an artwork above keeping its clock exact
(p3a behaves the same), and prefers a single memory story over a pre-decode cache whose
budget and eviction would be a second playback engine. The cost is that a heavy 128x128
WebP or APNG plays in slow motion on the ESP32-S3 rather than at speed; the first
firmware milestone measures how heavy "heavy" is.

## Considered options

- Time-accurate with frame skipping: exact timing, but visibly missing frames on the
  artworks this device exists to show.
- Pre-decoding whole animations into PSRAM (12 KB per output frame): guarantees smooth
  playback, but doubles the playback machinery and delays swaps while the next artwork
  decodes in full.

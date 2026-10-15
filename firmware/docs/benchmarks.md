# Decode benchmark

[Developer guide](../README.md) · [Project status](../../docs/status.md)

Historical measurement, 2026-09-19; these sample results are not guarantees for every file.
All commands below run from `firmware/`.


Measured on the device with `GET /api/v1/diag/bench` (decode plus scaling to 64x64, per
frame, on the HTTP task on core 0 while the show ran on core 1; Pillow-made corpus from
`tests/host/corpus/`, two loops):

| File | Format | Canvas | Frames | Avg ms/frame | Max ms | Sustainable fps |
|---|---|---|---|---|---|---|
| gif_anim_32.gif | GIF | 32x32 | 8 | 0.98 | 1.07 | 1016 |
| apng_blend_48.png | APNG | 48x48 | 16 | 3.46 | 4.29 | 289 |
| apng_opaque_64.png | APNG | 64x64 | 12 | 4.73 | 5.37 | 212 |
| webp_anim_lossless_64.webp | WebP | 64x64 | 12 | 5.66 | 6.61 | 177 |
| apng_rgba_128.png | APNG | 128x128 | 8 | 22.30 | 25.04 | 45 |
| webp_anim_lossy_128.webp | WebP | 128x128 | 10 | 28.68 | 31.07 | 35 |
| png_rgba_128.png | PNG | 128x128 | 1 | 6.27 | 6.27 | (static) |
| png_rgb_256.png | PNG | 256x256 | 1 | 15.87 | 15.87 | (static) |
| bmp_24_64.bmp | BMP | 64x64 | 1 | 1.01 | 1.01 | (static) |

So 64x64 animations decode well inside the 16.7 ms budget of 60 fps in every format, and
128x128 animations sustain 35 to 45 fps: 128x128 at 60 fps plays in slow motion under
the no-drop rule (ADR 0003), as the spec allows. Run it again after decoder or scaler
changes: `python tests/device/api_smoke.py http://p64.local --corpus --bench`.

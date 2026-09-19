# animatedgif (vendored)

[bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) by Larry Bank,
Apache-2.0 (see `LICENSE`). Copy of `src/AnimatedGIF.h`, `src/AnimatedGIF.cpp`
and `src/gif.inl` from upstream commit `c2478eca7aa2f3b7a09bdc583e5a4a43d03ac0d9`
(2026-07-03), with one local patch in `gif.inl` (search for "p64 patch"): the
"file too small" check in `GIFParseInfo()` rejected valid GIFs shorter than about
255 + palette bytes, because it judged by the second read's byte count instead of
the bytes already in the buffer. Three of the Makapix GIFs (200-265 bytes) hit
this. The ESP32-S3 SIMD
assembly files (`s3_transparent.S`, `p4_transparent.S`) and `GIFPlayer.h` are
not copied: they only serve the library's "cooked" RGB565 output mode, which
p64 does not use.

How p64 uses it: `GIF_DRAW_RAW` mode with an RGB888 palette. The library
decodes LZW and calls our line callback with palette indices, the frame's
rectangle, its transparency index and its disposal method; compositing into a
canvas (including disposal method 3, which the library itself does not
implement) lives in `main/gif_player.cpp`.

Build flags set by `CMakeLists.txt`: `__LINUX__` (its non-Arduino build),
`ALLOWS_UNALIGNED`, and `-w` for this component only.

To update: copy the three files from a newer upstream commit, record the
commit here, rebuild, and run `tools/gifcheck/gifcheck.py`.

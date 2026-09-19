# libwebp in p64

Vendored from https://github.com/webmproject/libwebp tag v1.4.0 (BSD-3-Clause, see
COPYING and PATENTS), decoder side only: `src/dec`, `src/demux` (demux and
WebPAnimDecoder), the decode sets of `src/dsp` and `src/utils` (the `COMMON_SOURCES` of
their Makefile.am plus the SIMD variants of those files, which compile to nothing on the
ESP32-S3), and the public headers in `src/webp`. The encoder, mux, sharpyuv and the
tools are left out. No source is modified. Threads are not enabled (`WEBP_USE_THREAD`
undefined). p3a fetches the same version at configure time; p64 vendors it so builds are
reproducible offline.

To upgrade: repeat the copy with the new tag (the file sets are listed in
`src/*/Makefile.am`), keep this note and the licence files current.

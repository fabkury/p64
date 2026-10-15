# Optional extensions and reference checkouts

[Developer guide](../README.md)

None of these repositories is needed to build the public firmware. Paths and commands
below are relative to `firmware/`.

## Private area

`private/` is a separate repository, `github.com/fabkury/p64-private`, never published:
the user's own components (a private source of channels, ADR 0012). It is git-ignored
here and the pre-commit hook refuses its paths. The build finds it on its own
(`CMakeLists.txt`): its `components/` join the build, `sdkconfig.private` applies on top of
`sdkconfig.defaults`, the version ends in `+private`, `main.cpp` calls `p64::priv::start()`
under `CONFIG_P64_PRIVATE`, and `tests/host/run.py` compiles what
`private/tests/host/manifest.json` names. Without the folder the build is the public
firmware, which is what CI builds. To mount it on a fresh checkout, from `firmware/`:

```
git clone https://github.com/fabkury/p64-private private
```

A private build must not install a public release without knowing it drops the private
parts; the Update page says so (`private_build` in `/api/v1/update`).

## Reference clones

`reference/` holds local clones of upstream repositories, git-ignored (the tracked
`hardware-tests/` beside them is the exception). Waveshare's example repository for the
driver board (schematic, examples, pin map) is the one the hardware tests were written
against; to re-create it, from `firmware/`:

```
git clone https://github.com/waveshareteam/ESP32-S3-RGB-Matrix reference/ESP32-S3-RGB-Matrix
```

Two more clones live there since 2026-09-19 and are used as references by the
specification: `p3a/`, the user's production ESP32-P4 pixel-art player whose module
boundaries, web UI and Makapix client p64 follows, and `makapix/`, the Makapix Club
server, whose `docs/player/`, `docs/mqtt-api/` and `api/openapi.json` are the device
contract:

```
git clone https://github.com/fabkury/p3a reference/p3a
git clone https://github.com/fabkury/makapix reference/makapix
```

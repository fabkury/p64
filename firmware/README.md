# p64 firmware

ESP-IDF v5.5.4, C++20, for the Waveshare ESP32-S3-RGB-Matrix and P2 64×64 panel.
The same firmware supports p64a and p64b.

[First build and flash](../docs/build-your-own.md#3-build-and-flash) ·
[Testing](docs/testing.md) · [API](docs/api.md) · [Architecture](docs/architecture.md)

## Build and flash

For a fresh machine, follow the linked build guide first. Commands below run from
`firmware/`, in PowerShell 7, with the paths in [tools/env.ps1](tools/env.ps1) set to
your ESP-IDF installation. Connect the board's **USB** port before flashing.

```powershell
./tools/build.ps1
./tools/flash.ps1
./tools/monitor.ps1
```

Exit the monitor with Ctrl+]. Use `./tools/flash.ps1 -Port COM7` to override port
detection, or `./tools/idf.ps1 <args>` for other IDF commands. Linux CI uses ESP-IDF
v5.5.4; the first-build guide also gives standard IDF commands for Linux/macOS.

[sdkconfig.defaults](sdkconfig.defaults) supplies project defaults. An existing generated
`sdkconfig` takes precedence: preserve any local overrides, then remove that generated
file when you need to regenerate from changed defaults. Optional development Wi-Fi
credentials belong only in git-ignored `sdkconfig.secrets`; use
[sdkconfig.secrets.example](sdkconfig.secrets.example) as the template.

**After OTA:** the device may boot `ota_1`, while the flash script writes `ota_0`.
Return to the previous slot through the Update page before relying on a serial flash;
check the reported running version afterward.

## Test a change

With native gcc/g++, Python and Pillow available, and IDF dependencies configured:

```powershell
python tests/host/run.py --werror
python tools/check_size.py
```

Run the size check in the ESP-IDF Python environment after a firmware build. See [testing](docs/testing.md) for dependency
setup, device checks and their side effects. Resource floors are in [budgets.json](budgets.json);
changes to them require a reason and measurements, not just a passing test.

## Find the code

| Area | Location |
|---|---|
| Boot, show state machine and loading | [main](main/) |
| Product modules | [components](components/); ownership and task rules in [architecture](docs/architecture.md) |
| Browser UI | [p64_web/ui](components/p64_web/ui/): Home, Playsets, Settings, Update |
| Asset sources | [assets](assets/) |
| Host and device tests | [tests](tests/) |

The web UI is embedded in the firmware; editing it requires rebuilding and flashing.
Its shared stylesheet and themes originated in p3a. Review upstream changes before
bringing them across. Generated fonts and icons are committed, so a normal build does
not need to regenerate them.

## Tools and references

- [Streaming guide](../docs/using-p64.md#send-a-live-stream) and [wire protocol](docs/api.md#streams).
- Asset tools: [fonts](tools/gen_fonts.py), [weather icons](tools/gen_weather_icons.py),
  [UI icons](tools/gen_ui_icons.py), [clock assets](tools/gen_clock_assets.py).
  Python tools document their inputs; image generators use Pillow.
- [Release asset builder](tools/release_assets.py) and [update contract](docs/api.md#updates).
- [CPU and heap sampling](tools/cpu_sample.py), [decode benchmark](docs/benchmarks.md).
- [Specification](../docs/spec/p64-spec.md), [glossary](../CONTEXT.md), [ADRs](../docs/adr/).
- [Current readiness](../docs/status.md) and [engineering history](docs/PROGRESS.md).
- [Optional extensions and reference checkouts](docs/extensions-and-references.md).

The [hardware-test firmware](reference/hardware-tests/README.md) records bring-up
findings: pins, DMA, pacing and hardware limits. Its experimental architecture is not
the product architecture.

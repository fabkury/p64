# p64 — LED matrix frame (desktop + wall)

A 3D-printable enclosure for a 64×64 LED matrix "art frame" that stands on a
desk on a detachable easel stand, or hangs dead-flush on a wall.

## Hardware

| Part | Size | Notes |
|---|---|---|
| Waveshare [RGB-Matrix-P2-64x64](https://www.waveshare.com/wiki/RGB-Matrix-P2-64x64) | 128 × 128 × 14.5 mm | HUB75E in/out + VH4 5V power on the back |
| Waveshare ESP32-S3-RGB-Matrix driver | 50.01 × 42.0 mm PCB | Plugs onto the panel's HUB75 input, **USB-C ports facing down** |

## Printed parts (`p64.scad`, set `part=`)

| `part` | What | Print orientation |
|---|---|---|
| `fitcheck` | 4 mm front slice of the frame — print this **first** to verify panel fit | as-is |
| `frame` | Main enclosure, 134 × 134 × 36.5 mm | back face on the bed |
| `stand` | Detachable 12° desk stand, 90 mm wide | lying on its side |
| `print` | frame + stand pre-laid-out in print orientation | — |
| `assembly` | Visual check: unit on its stand on a desk | — |

Export: `openscad -o p64_frame.stl -D "part=\"frame\"" p64.scad` (same for the others).

## Design

- **Bezel-less front** — the LED face sits flush with the frame's front edge;
  all 64×64 pixels visible. The panel is retained by 4× M3×12 screws driven
  from the back, down counterbored tubes, into the panel's own threaded inserts.
- **Desk mode** — the stand slides onto a T-rail dovetail in the lower back;
  the frame's bottom edge rests on the stand's lip; 12° lean-back. A cable
  channel through the stand routes the USB-C lead down and out the rear.
- **Wall mode** — pop the stand off; the back is completely flat. Two keyhole
  hangers on 80 mm centers (screw head Ø 7–8 mm) hold it flush like a frame.
  The USB-C cable exits through the bottom edge.
- **Access without opening**: both USB-C ports (bottom window), microSD
  (back window over the push-push slot), BOOT/RESET (back window), plus
  ventilation slots.

## BOM

- 4 × M3 × 12 machine screws
- Wall mode: 2 × wall screws/anchors, head Ø 7–8 mm, on 80 mm horizontal centers

## ⚠ Measure before printing

Defaults marked `MEASURE ME` in `p64.scad` were **estimated from product
photos** — verify on your hardware and adjust:

| Param | Meaning | Default (estimate) |
|---|---|---|
| `hole_dx`, `hole_dy` | Panel M3 insert spacing (centered grid) | 113 × 90 mm |
| `drv_cx`, `drv_cy` | Driver PCB centre, from panel centre, viewed from the back | (−34.7, 10) |
| `drv_standoff` | Panel back → driver PCB back with connector mated | 10 mm |

All port cutouts (USB window, SD window, BOOT/RST window) are derived from
`drv_cx`/`drv_cy`, so fixing those two numbers fixes everything. Dry-fit the
panel + driver, measure, update, re-render. Print `fitcheck` (a few grams)
before committing to the full frame.

## Print settings

- PETG or PLA, 0.4 mm nozzle, 0.2 mm layers, 3 walls; no supports needed
  (the T-slot cap channel bridges ~44 mm — fine on most printers; enable
  supports-in-cavity if yours struggles).
- Clearances: panel pocket 0.3/side, T-rail 0.3/side — tune `panel_clr`
  and `rail_clr` if your printer runs tight or loose.

## Assembly

1. Plug the driver onto the panel's HUB75 input (USB-C down); wire panel
   power (VH4) from the driver's 5V/GND pads.
2. Slide panel + driver into the frame from the front.
3. Drop 4 × M3×12 into the counterbores in the back; drive them into the
   panel inserts with a long screwdriver.
4. Desk: slide the frame down onto the stand's rail. Wall: hang the keyholes
   on two screws.

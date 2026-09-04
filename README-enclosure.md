# Tabletop enclosure for the Waveshare RGB-Matrix-P2-64x64 + ESP32-S3-RGB-Matrix

A single-piece, support-free 3D printed back shell that screws onto the six M3 brass
inserts of the 128 x 128 mm P2 LED matrix, encloses the ESP32-S3-RGB-Matrix controller
that is plugged into the panel's HUB75 IN header, and stands the display on a desk
leaning back 12 degrees.

## Files

| File | What it is |
|---|---|
| `p64_enclosure.scad` | Parametric OpenSCAD source (OpenSCAD 2021.01+). Everything below is a parameter. |
| `p64_enclosure_print.stl` | Ready to slice, already in print orientation (back face on the bed). |
| `p64_enclosure_print.3mf` | Same mesh, 3MF. |
| `render_*.png` | Preview renders (back, front with mock-ups, side section, bottom). |

## What the design does

- **Fit:** pocket 128.4 x 128.4 mm (0.3 mm clearance per side around the 127.8 mm frame
  measured in Waveshare's drawing), 2 mm walls, 2.4 mm back wall.
  The walls wrap 12 mm forward over the panel's plastic frame only, so the LED/mask edge
  stands ~2.5 mm proud and the front is nearly bezel-less.
- **Size:** 132.4 mm wide, 132.4 mm tall at the back edge and 139.6 mm at the front (the
  base wedge). The side profile is a wedge: 34 mm deep at the bottom (12 mm lip + 22 mm
  shell) thinning to 20 mm at the top (12 + 8). The back face is one flat plane sloping
  6 degrees, so the print still lies flat on it.
- **Interior:** 19.4 mm clear at the bottom edge, 12.9 mm at the top edge of the controller,
  5.8 mm at the top edge of the panel. The mounted controller needs roughly 9 mm and the
  VH4 power plug with its wires about 8 mm, both in the deep half.
- **Mounting:** six counterbored bosses on the panel's inserts, positions from Waveshare's
  drawing: (0, +-56.85) and (+-56.85, +-44) mm, rotated with the panel. M3 x 10 screws go in
  from the back; the boss floor is 5 mm so about 5 mm of thread engages the insert. The two
  top bosses are only 6.6 mm tall because of the wedge; their screw heads end up 1 mm below
  the back face (keep `depth_top` at 8 mm or more, or use M3 x 8 there).
  A 1.6 mm seating ledge with a 45-degree underside supports the frame's 1.6 mm outer wall all around.
- **Tilt / stand:** the bottom wall thickens into a wedge so the whole base is one flat plane
  at 12 degrees. Base contact patch is 34 mm deep; the centre of gravity lands about 9 mm
  in front of the rear edge, so it takes roughly an 8-degree backward push to tip.
- **Panel orientation:** the panel is installed rotated 90 degrees counter-clockwise (seen
  from the back) so the controller sits at the bottom with its USB-C ports facing down.
  The LED image must be rotated 90 degrees in firmware (a one-line setting in most HUB75
  libraries; try both directions).
- **Cable:** a right-angle (L-shaped, "up/down angled": cable leaves perpendicular to the
  plug's wide face) USB-C cable plugs into either port from underneath through a
  28 x 12.5 mm pocket in the base, then runs in a groove under the base to a notch at the
  bottom of the back wall. Nothing is visible from the front or sides.
- **Back wall:** two bands of ventilation slots, 3.5 mm pin holes over BOOT and RESET with
  debossed labels, 3.5 mm holes over the two microphones, six screw counterbores.

Not included: access to the TF card slot. In every orientation the card slot ends up about
39 mm from the nearest wall, so a slot in the shell would be useless. The card has to be
inserted before closing the shell.

## Assembly

1. Plug the controller onto the panel's HUB75 IN header and wire its 5V/GND screw terminals
   to the panel's VH4 power socket, exactly as in Waveshare's photo (ignore the speaker).
2. Slide the panel into the shell from the front with the controller at the **bottom**
   (USB-C ports pointing at the wedge). It seats on the ledge and the six bosses.
3. Fit six M3 x 10 screws from the back. The heads sit 17 mm deep, so use a long PH1 or
   hex driver.
4. From underneath, push the right-angle USB-C plug up into the POWER port (power only) or
   the USB port (power + programming). Lay the cable in the groove toward the back notch.
5. Rotate the image 90 degrees in firmware.

## Printing

- Print `p64_enclosure_print.stl` as delivered: it already lies on its inclined back face,
  no supports needed. The walls lean 6 degrees, the base face overhangs 6 degrees, the
  ledge underside is 45 degrees, and there are 6.5 mm bridges over the screw counterbores.
  Print height is 34.6 mm; about 80 g of PLA with 20 percent infill.
- 0.2 mm layers, 3 to 4 perimeters, 20 percent infill, PLA or PETG.
- Enable elephant-foot compensation (0.1 to 0.2 mm) so the screw counterbores and the
  debossed labels stay clean on the first layer.
- Bed needs at least 140 x 140 mm.

## Verified against the Waveshare drawing

`RGB-Matrix-P2-64x64-2D.dwg` (in this folder, identical to Waveshare's GitHub copy) contains
the plastic frame ("JXS-P2-128*128 bottom case") and the LED mask, not the PCB. Checked:

| Item | Drawing | Model |
|---|---|---|
| Six M3 inserts | (0, +-56.85), (+-56.85, +-44), M3, on dia 9.8 faces | same to 0.01 mm; boss dia 10 |
| Frame outline | 127.8 x 127.8, sharp corners, 12 mm deep, no draft | pocket 128.4 (0.3 mm per side), corner r 0.6, lip 12 |
| Outer wall / rim for the ledge | 1.6 mm thick (inner face at +-62.3) | ledge 1.6 mm, sits fully on the wall |
| Back opening | +-51.9, widening to ~+-58.4 for y within +-16 | USB-C ports fall inside the wide part |
| Corner screw recesses | dia 6 at (+-56, +-56), M1 x 6 screws | inside the cavity, 3.7 mm from the ledge |
| Pins | dia 3 x 3 mm at (+-56, -29) | inside the cavity, clear of ledge and bosses |

The controller's own dimensions come from `ESP32-S3-RGB-Matrix-2D.pdf` (1:1 vector PDF).

## Things that could not be verified

| Input | Value used | Parameter | Notes |
|---|---|---|---|
| Centre of the panel's HUB75 IN header | (-35.0, +5.4) mm from the panel centre, panel arrows up, seen from the back | `hub75_in_native` | From the product photo, +-1 mm. Pin holes (3.5 mm), mic holes (3.5 mm) and the plug pocket (30 mm) are sized to absorb that error. With this value the chip's edge overhangs the frame rim by 0.8 mm, which fits the photo. |
| Height of the controller PCB's back face above the frame's back face | 0.5 mm | `z_chip` | Must be 0 or more because the chip edge overhangs the rim. Only the plug pocket (`pocket_z`) depends on it. |
| Right-angle plug body | up to 12 wide x 7 thick x 14 long | `pocket_w`, `pocket_z` | Enlarge the pocket if yours is bigger. |

Quick verification with the printed part: the panel should drop into the pocket by hand;
the six holes should line up with the inserts without forcing.

## Main parameters (`p64_enclosure.scad`)

- `depth_bottom` 22, `depth_top` 8 (set equal for a flat back), `tilt` 12, `lip` 12,
  `wall` 2, `back_t` 2.4, `panel_clr` 0.3
- `screw_len` 10, `engage` 5, `cb_d` 6.5, `boss_od` 10
- `panel_rot` 90 (set 0 for the un-rotated panel; the cable pocket then no longer applies)
- `vents`, `pin_d`, `mic_d`, `label_size`
- `part` selects `shell`, `print`, `assembly`, `section_x`, `section_y`

Regenerate the STL with:

```
openscad -o p64_enclosure_print.stl -D "part=\"print\"" p64_enclosure.scad
```

## Sources

- Panel drawing: https://github.com/waveshareteam/RGB-Matrix-Px-xx/tree/main/hardware/dimensions/RGB-Matrix-Pxx-64x64 (`RGB-Matrix-P2-64x64-2D.dwg`)
- Controller drawing: https://github.com/waveshareteam/ESP32-S3-RGB-Matrix/tree/main/hardware/dimensions (`ESP32-S3-RGB-Matrix-2D.pdf`)
- Wikis: https://docs.waveshare.com/ESP32-S3-RGB-Matrix and https://docs.waveshare.com/RGB-Matrix-Px-64x64

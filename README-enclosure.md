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

- **Fit:** pocket 128.6 x 128.6 mm (0.3 mm clearance per side), 2 mm walls, 2.4 mm back wall.
  The walls wrap 12 mm forward over the panel's plastic frame only, so the LED/mask edge
  stands ~2.5 mm proud and the front is nearly bezel-less.
- **Size:** 132.6 mm wide, 132.6 mm tall at the back and 139.8 mm at the front (the wedge),
  34 mm deep overall (12 mm lip + 22 mm shell). Total thickness with the panel: ~34.5 mm.
- **Interior:** 19.6 mm clear behind the frame's back face. The mounted controller needs
  roughly 9 mm, so there is ample room for the VH4 power plug and its wires.
- **Mounting:** six counterbored bosses on the panel's inserts, positions from Waveshare's
  drawing: (0, +-56.85) and (+-56.85, +-44) mm, rotated with the panel. M3 x 10 screws go in
  from the back; the boss floor is 5 mm so about 5 mm of thread engages the insert.
  A 1.5 mm seating ledge with a 45-degree underside supports the frame's outer rim all around.
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
- **Back wall:** two bands of ventilation slots, 2.6 mm pin holes over BOOT and RESET with
  debossed labels, 3 mm holes over the two microphones, six screw counterbores.

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

- Print `p64_enclosure_print.stl` as delivered: back face on the bed, no supports needed.
  The only overhangs are the 12-degree base face, the 45-degree ledge and 6.5 mm bridges
  over the screw counterbores.
- 0.2 mm layers, 3 to 4 perimeters, 20 percent infill, PLA or PETG.
- Enable elephant-foot compensation (0.1 to 0.2 mm) so the screw counterbores and the
  debossed labels stay clean on the first layer.
- Bed needs at least 140 x 140 mm.

## Things to check before printing

The panel drawing and the controller drawing are exact (both from Waveshare's GitHub), but
three inputs were taken from photos or assumed. Each is a single parameter in the `.scad`.

| Input | Value used | Parameter | If it is different |
|---|---|---|---|
| Height of the controller PCB's back face above the frame's back face | 1.5 mm (assumed 0 to 3) | `z_chip` | Interior has 10+ mm of margin; only the plug pocket (`pocket_z`) cares. |
| Centre of the panel's HUB75 IN header | (-35.0, +5.4) mm from the panel centre, panel arrows up, seen from the back | `hub75_in_native` | Shifts the pin holes, mic holes and plug pocket by the same amount. Measure with a ruler if you can: distance from the header centre to the top-centre insert. |
| Right-angle plug body | up to 12 wide x 7 thick x 14 long | `pocket_w`, `pocket_z` | Enlarge the pocket. |
| Frame corner radius | pocket corner 0.8 mm | `r_in` | Reduce if the frame corners are sharper. |

Quick verification with the printed part: the panel should drop into the pocket by hand;
the six holes should line up with the inserts without forcing.

## Main parameters (`p64_enclosure.scad`)

- `depth` 22, `tilt` 12, `lip` 12, `wall` 2, `back_t` 2.4, `panel_clr` 0.3
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

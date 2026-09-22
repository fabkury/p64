# Hand measurements

Parts measured by hand for the enclosure. Values are in millimetres unless noted.
Add new parts or new measurements below; keep one section per part. (The speaker box
that only v5 used is measured in `../archive/pre-v7/README.md`.)

## v1 shell print (received 2026-09-18)

`output/v1/p64_enclosure_service.stl` (0.45 mm fit clearance), ordered from JLC3DP on
2026-09-05 (order D2026090531500023, $18.83 including shipping), printed in black FDM
PLA. Photos, all taken on
2026-09-18:

| File | Shows |
|---|---|
| `PXL_20260918_170429654.jpg` | Open shell, cavity up, beside the panel with the controller and power leads |
| `PXL_20260918_170508326.jpg` | Assembled display in hand, lit, cable leaving the base pocket |
| `PXL_20260918_170630166.jpg` | Assembled display standing on the desk, front, off |
| `PXL_20260918_170639495.jpg` | Back face standing: six counterbores, vents, pin holes with groove marks, mic holes, cable notch |
| `PXL_20260918_170650583.jpg` | Side view standing: wedge profile, frame edge proud of the shell |
| `PXL_20260918_233146091.jpg` | Panel back with the controller out of the shell, next to the short right-angle USB-C cable |
| `PXL_20260918_234736918.MP.jpg` | Display standing, lit, in the dark |

Measured on 2026-09-18:

| Item | Value | Model |
|---|---|---|
| Width | 133.0 | 132.4 |
| Depth at the bottom edge, front lip to back face | 34.6 | 34.6 |
| Panel side play in the pocket | under 1 | 0.45 per side |

Notes:
- All six bosses line up with the inserts; screw holes and counterbores printed clean.
- The frame sits on the ledge all round with the LED mask about 2.5 mm proud.
- The base pocket lands exactly on the two USB-C ports.
- Visible gap between the controller and the back wall.
- Stands stably at 12 degrees; barely warm after running for a while.
- Not checked yet: pin and mic holes over the buttons and mics; the plug in the pocket and
  the cable in the groove to the back notch.

## 90-degree USB-C adapter (two, one per driver-board port)

Small aluminium-shelled USB-C male-to-female right-angle adapter: the male plug leaves the
wide face of the body near one end, the female socket sits in the far end face. Two are
used, one on each of the driver board's ports (POWER and USB), and they stay there; the
external cable plugs into the adapter's socket from the back of the shell (v6). Photos in
`usb-c-90-degree-adapter/`: `PXL_20260919_152802345.jpg` (side, plug to the right),
`PXL_20260919_152820195.jpg` (female end face), `PXL_20260919_152837459.jpg` (plug end),
and `PXL_20260919_152802345 - with measures.jpeg` (the side view with the measurements
drawn on; the GIMP source `.xcf` is kept next to it, untracked).

Measured on 2026-09-19 (the side photo):

| Item | Value |
|---|---|
| Body length, near end face to female socket face | 19.3 |
| Body thickness (along the plug axis) | 8.0 |
| Near end face to the plug tip, along the body | 13.3 (so the plug's far edge is 6.0 from the near end) |
| Body face to the plug tip, along the plug axis | 15.7 - 8.0 = 7.7 |
| Body width across the end face | 12.7 (the two bodies touch when both are plugged in: the ports are 12.74 apart) |

Derived, used by v6: the plug's centre is 4.8 from the near end (6.0 minus half of the
2.4 mm plug thickness); the boot between the body face and the receptacle face is about
1.2 (7.7 minus a 6.5 mm plug shell), an estimate.

Checked on the panel the same day:
- Both adapters fit in the two ports at once; the bodies touch.
- Plugged into a port with the controller on the panel, the adapter body lands on the
  frame's back plate below the port and stops about 1 mm short of fully seated. The
  shell design (v7) therefore assumes a notch cut through the plate strip below the
  ports, see `README.md` (v6).

## Adafruit 5880 rotary encoder breakout (p64b; two on hand, not measured yet)

The shell's numbers for the board come from Adafruit's EagleCAD file
(`adafruit-5880/README.md`) and for the encoder from the Bourns PEC11 datasheet, checked
on 2026-09-22; three inputs are in neither source. Fill the table with calipers when the
boards are at hand; a value that differs from the model changes the shell (`enc_*` in
`src/p64_enclosure_v7.scad`, then regenerate p64b).

| Item | Model | Measured | Sets |
|---|---|---|---|
| Shaft tip to the bushing base (the body's top face) | 15.0 | | `enc_shaft_l`; 15 = the 5.0 mm bushing, 20 = 7.0 (`enc_bush_l`) |
| Threaded bushing length | 5.0 | | `enc_bush_l`: post height, thread left for the nut through the 2.4 mm wall |
| PCB top face (component side) to the bushing base | 6.5 | | `enc_body_h` |
| PCB thickness | 1.6 | | `enc_board_t` (peg length is board + 0.4) |
| Washer + nut stacked, height | 2.5 | | `enc_nut_h` (needs to be at or under the thread left, 3.0) |
| Mounting hole spacing (centre to centre) | 20.32 | | `enc_hole_p` (board file; a check) |
| Mounting hole diameter | 2.5 | | `enc_hole_d` (board file; the 2.2 mm pegs) |
| Board width | 25.4 | | `enc_board` (board file; a check) |
| Knob: diameter and height, if bought | 20 x 12.5 | | mock-up only (`knob_d`, `knob_h`) |

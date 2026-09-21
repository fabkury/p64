# Tabletop enclosure for the Waveshare RGB-Matrix-P2-64x64 + ESP32-S3-RGB-Matrix

This folder is the enclosure part of the [p64 project](../README.md). All paths and
commands below are relative to `enclosure/`.

A single-piece, support-free 3D printed back shell that screws onto the six M3 brass
inserts of the 128 x 128 mm P2 LED matrix, encloses the ESP32-S3-RGB-Matrix controller
that is plugged into the panel's HUB75 IN header, and stands the display on a desk
leaning back 12 degrees.

## Files

| Path | What it is |
|---|---|
| `src/p64_enclosure.scad` | Parametric OpenSCAD source (OpenSCAD 2021.01+). Everything below is a parameter. |
| `output/v1/p64_enclosure_print.stl` | v1, ready to slice, already in print orientation (back face on the bed). |
| `output/v1/p64_enclosure_print.3mf` | Same mesh, 3MF. |
| `output/v1/p64_enclosure_service.stl` | v1 bureau variant (MJF/SLA): 0.45 mm fit clearance instead of 0.3 mm. The file ordered on 2026-09-05; it came back printed in FDM PLA on 2026-09-18 and fits, see [Verified with the v1 print](#verified-with-the-v1-print). |
| `output/v1/render_*.png` | v1 preview renders (back, front with mock-ups, side, sections, bottom, print orientation). |
| `src/p64_enclosure_v2.scad` | **v2**: v1 plus two rotary encoders on the back face (see [v2](#v2-two-rotary-encoders-on-the-back)). Kept as a separate file so v1 stays as printed. |
| `output/v2/` | v2 outputs under the same file names: `p64_enclosure_print.stl` / `.3mf`, `p64_enclosure_service.stl` (0.45 mm clearance), and renders (back, assembly with knobs, section through an encoder, print orientation, and `render_product.png` / `render_product_back.png`: the assembled display standing on a table, seen from the front-left and from the back-right with the knobs). |
| `src/p64_enclosure_v3.scad` | **v3** (kept alternative, v2 remains the version to print): v2 with the stand's wedge turned into a recessed plinth (see [v3](#v3-recessed-plinth)), so the front rim is 2 mm on all four sides. |
| `output/v3/` | v3 outputs, same file names and renders as v2. |
| `src/p64_enclosure_v4.scad` | **v4** (superseded by v6): v2 plus two panel-mount USB-C sockets on the back face for POWER and USB, see [v4](#v4-panel-mount-usb-c-sockets). The v1 print showed there is no room inside for the sockets. |
| `output/v4/` | v4 outputs, same file names as v2, plus `render_section_usb.png` through the POWER socket. |
| `src/p64_enclosure_v5.scad` | **v5** (kept alternative, v4 remains the version to print): v4 plus the speaker shipped with the controller, sunk into the upper back and firing backwards, see [v5](#v5-speaker-in-the-back). |
| `output/v5/` | v5 outputs, same file names as v4, plus `render_section_spk.png` through the speaker's lugs. |
| `src/p64_enclosure_v6.scad` | **v6** (the version to print): v4 with the panel-mount sockets replaced by two small 90-degree USB-C adapters that stay on the controller's ports, reached through one window in the back face, see [v6](#v6-90-degree-adapters-on-the-ports). Needs a hand-cut notch in the panel frame. Edited in place on 2026-09-19: the outer edges are chamfered and filleted for the hand, see [Edges](#edges-2026-09-19-edited-in-place). |
| `output/v6/` | v6 outputs, same file names as v4, plus `render_section_ad.png` (through the POWER adapter), `render_section_win.png` (along the adapters' centre line), `render_frame_notch.png` (where to notch the panel frame, in red) and `render_section_edge.png` (the wall profile through x = 33: the edge chamfer and fillets). |
| `input/measurements.md` | Hand measurements of parts that have no drawing (the speaker, the v1 print, the 90-degree USB-C adapter). |
| `input/PXL_20260911_*.jpg` | Photos of the speaker box: front, front with lead, corner lug. |
| `input/usb-c-90-degree-adapter/` | Photos of the 90-degree USB-C adapter used from v6 on; one has the hand measurements drawn on it. |

Each shell version gets its own sub-folder under `output/` with identical file names inside;
the version lives in the folder name (and in the source file name under `src/`).
| `input/RGB-Matrix-P2-64x64-2D.dwg`, `.pdf` | Waveshare's drawing of the panel frame (DWG and a PDF rendering of it). |
| `input/ESP32-S3-RGB-Matrix-2D.pdf` | Waveshare's 1:1 drawing of the controller board. |
| `input/*.jpg` | Waveshare product photos used for the features the drawings do not cover. |
| `archive/2026-08-stand-concept/` | Earlier, abandoned concept (detachable stand, wall keyholes). Kept for reference only. |
| `archive/2026-09-dhruv-solidworks/` | Dhruv's separate SolidWorks take on the enclosure (`p64.SLDPRT` and its STEP export). Kept as-is, never edited here. |

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
  drawing: (0, +-56.85) and (+-56.85, +-44) mm, rotated with the panel. Bosses are 10.5 mm in
  diameter around a 6.5 mm counterbore (2 mm walls). M3 x 10 screws go in
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
- **Back wall:** two bands of ventilation slots, 3 mm pin holes over BOOT and RESET marked
  with short grooves (one groove = BOOT, two grooves = RESET; text labels were dropped because
  their 0.3 mm ribs are too thin for bureau printing), 3.5 mm holes over the two microphones,
  six screw counterbores. Every wall and rib is 2.0 mm or thicker.

Not included: access to the TF card slot. In every orientation the card slot ends up about
39 mm from the nearest wall, so a slot in the shell would be useless. The card has to be
inserted before closing the shell.

## v2: two rotary encoders on the back

`src/p64_enclosure_v2.scad` is v1 with two rotary encoders for user input added; nothing
else changed. It is a separate file so that v1, the print ordered on 2026-09-05, stays as it
was. Set `encoders = false` in v2 to get the v1 geometry back.

- **Hardware assumed:** two Adafruit 5880 boards (I2C "seesaw" rotary encoder breakout:
  25.4 x 25.4 mm PCB with four 2.5 mm plated holes on a 20.32 mm square, a Bourns
  PEC11-style 24-detent encoder with push switch soldered at the centre, 15 mm D-shaft,
  M7 x 0.75 bushing 5 mm long, 6.5 mm body height) and 20 mm set-screw knobs. Board
  geometry comes from Adafruit's EagleCAD file, the encoder from the Bourns PEC11 datasheet
  (links under Sources). The 20 mm-shaft PEC11R has a 7 mm bushing: set `enc_bush_l = 7`.
- **Where:** the shafts leave the back face at (+-47, -25) mm in the shell's coordinates,
  i.e. 19 mm in from each side edge and 41 mm up from the bottom edge, one knob per side.
  That spot is clear of the (+-56.85, 0) and (+-44, -56.85) bosses, the controller
  (x = -16..26) and the cable pocket, and needs no extra shell depth.
- **How they mount:** each board lies parallel to the back face, component side towards the
  wall. Four 4.5 mm posts hang from the cavity back and end in 2.2 mm pegs that enter the
  board's holes, so the board cannot turn. The bushing passes through a 7.4 mm hole; a
  14 mm, 0.4 mm-deep spot-face on the outside gives the supplied washer and nut a flat seat
  on the sloping wall and 3.0 mm of thread (the bushing stands 2.6 mm proud of the face).
  The nut takes the knob's push force; the pegs only key the board.
- **Numbers** (printed by the `echo` lines): cavity 15.2 mm deep at the encoder; board
  bottom 7.2 mm behind the frame back face (ledge top is at 3.1 mm); posts 8.3 mm from the
  nearest boss; board edge 4.5 mm from the side wall; shaft 12.6 mm proud of the face; knob
  end 32 mm behind the frame back face. The knobs are therefore the deepest point of the
  shell, 10 mm past the bottom edge: the display stands exactly as before, but laid on its
  back it rests on the knobs.
- **Vents:** the lower band loses its outer two columns on each side (x = +-36 and +-42)
  where the boards sit. Everything else on the back wall is as in v1.
- **Board orientation:** the side wall is only 4.5 mm from the board's outer edge, so turn
  each board with its two STEMMA QT sockets facing up and down and the header pads towards
  the centre (`enc_rot`). The cables then run vertically past the boards.
- **Wiring (not part of the shell):** the two boards chain on the controller's 4-pin SH1.0
  "GPIO" socket (pin 1 = IO45, 2 = IO46, 3 = 3V3, 4 = GND) used as a second I2C bus. The
  STEMMA QT pin order differs, so use an SH-to-header cable pair rather than a straight
  SH-SH cable; IO45/IO46 carry 10 k pull-downs on the controller, so add 2.2 k pull-ups to
  3V3 on SDA and SCL. Second board: close jumper A0 (address 0x37).
  The complete wiring projects, with schematics, bench procedure and firmware plan, are
  `docs/hardware/encoders-a-solderless.md` (one encoder) and
  `docs/hardware/encoders-b-soldered.md` (two encoders).

Assembly additions, before step 2 of the list below: plug the STEMMA QT cables into both
boards; from inside the shell push each board onto its four pegs with the shaft through
the wall; from outside fit the washer and nut (hand-tight, 10 kgf.cm max) and then the
knob; connect the chain to the controller's GPIO socket before sliding the panel in.

Printing: as v1. The posts and pegs stand up from the bed, no supports. The pegs are
2.2 mm pins; MJF nylon prints them fine, for FDM check the fit and sand if needed. The
spot-faces sit on the bed face and become a 3.3 mm-wide bridge ring at the third layer,
which is harmless.

## v3: recessed plinth

`src/p64_enclosure_v3.scad` is v2 with one change to the stand (`plinth = true`;
`plinth = false` reproduces v2). It is kept as an alternative: on 2026-09-11 the user chose
to keep v2 as the version to print, mainly for its longer contact patch.

- **Why:** leaning back 12 degrees drops the shell's back-bottom edge 7 mm below the
  front-bottom edge, so a flat base needs 7.2 mm of material added below the front outline.
  In v1 and v2 that is the 9 mm band under the LED matrix (wedge plus the 2 mm wall). It
  cannot be taken from the back instead: the base plane would rise 5 mm into the cavity
  there, leaving the USB plug 2 to 3 mm below the base and opening the bottom screw bosses.
- **What v3 does:** the front outline keeps its 2 mm rim on all four sides, and the wedge
  starts 12 mm behind the front face, at the frame's back face (`plinth_z0 = 0`), as a
  4.7 mm plinth with a vertical front face. The base plane and the lean are exactly v2's,
  so the display stands at the same height and angle; the front lip floats 7.1 mm above
  the table at the front edge and 4.6 mm at the plinth.
- **Stance:** the contact patch is 22.5 mm deep instead of 34.8 (it starts at the plinth).
  The centre of gravity projects roughly 10 mm behind the plinth's front edge and 12 mm in
  front of the rear edge, so the display is about as hard to tip forwards as backwards.
- **What shows:** from the front, a uniform 2 mm rim and a shadow gap under it; from low
  angles the plinth's front face, and in it the 30 mm-wide plug pocket as a notch. The
  cable exit at the back is unchanged.
- **Printing:** same orientation. The plinth's front face is a 4.7 mm-wide shelf facing the
  front, fully supported; the base face overhangs 6 degrees as in v1. Print height 33.8 mm,
  bed footprint 132.4 x 137.6 mm, volume 74.4 cm3 (v2: 83.5).

## v4: panel-mount USB-C sockets

`src/p64_enclosure_v4.scad` is v2 (front bar, not the v3 plinth) plus two JUXINICE
panel-mount USB-C extensions (the 2-pack: 15 cm flat ribbon, USB 2.0, 100 W PD), one for
the POWER port and one for the USB programming port. The external cables plug into the
shell; the controller's ports only ever see the short internal ribbons.

- **Hardware assumed** (from the seller's drawings; confirm on the parts): flange
  25.2 x 8.2 mm with two threaded 2.5 mm holes 17 mm apart, 9 x 3.5 mm mouth, housing
  about 12 x 8.2 x 19.5 mm with the ribbon leaving its far end, L-shaped plug 11 x 11 mm
  whose ribbon leaves perpendicular to the plug's wide face.
- **Where:** socket centres on the back face at (-44, -45) for POWER and (+44, -45) for USB,
  mouths horizontal, 3.2 mm below each encoder board and 2.5 mm above each corner boss.
  Each housing runs forward through the frame's back opening and ends 2 mm in front of the
  frame back face, 10 mm above the panel PCB. The plugs hang 4.4 mm below the ports, 7 mm
  above the bottom wall, ribbons leaving towards the back where the cavity is 18.6 mm deep.
- **Wall features:** per socket a 9.6 x 4.1 mm stadium cut-out and two 2.8 mm holes; the
  supplied screws thread into the socket from outside, so no nuts or inserts. One groove
  outboard of the right socket marks it as USB; POWER has none. The lower vent band also
  loses its +-30 columns (flange keep-out), leaving nine.
- **Ribbon guides:** two printed hooks per ribbon on the cavity back (3 mm wide, 4 mm tall,
  2.5 mm lip with 2.5 mm under it) at x = +-22 and +-32, one above and one below the
  ribbon's line at y = -50.5, so each flat cable is held against the back wall between plug
  and socket and cannot drift onto the encoder boards.
- **Removed from v2:** the plug pocket in the base, the cable groove under the base and the
  notch at the back wall. Base and back edge are closed; the only openings are the sockets.
- **Assembly change:** plug both ribbons into the controller's ports before the panel slides
  in (ribbon exit towards the back; the plug is reversible), lead each ribbon under its two
  hooks, screw the sockets to the back face from outside, then continue as in v1.
- **Printing:** as v2. The hook lips overhang 2.5 mm at 2.5 mm height, which MJF prints
  without support and FDM handles with part cooling.

## v6: 90-degree adapters on the ports

`src/p64_enclosure_v6.scad` is v4 with the JUXINICE panel-mount sockets replaced. Handling
the v1 print showed there is no room inside the shell for the sockets' housings and their
ribbon plugs, so the external cables now plug into two small 90-degree USB-C adapters that
stay on the controller's ports for good, through one window in the back face. Everything
else is v4 (encoders, front bar, no speaker). Set `adapters = false` to get a shell with no
USB opening at all.

- **Hardware** (hand-measured on 2026-09-19, `input/measurements.md`, photos in
  `input/usb-c-90-degree-adapter/`): a 19.3 x 12.7 x 8.0 mm aluminium-shelled body; the
  male plug leaves the wide face with its centre 4.8 mm from one end (7.7 mm proud of the
  face, boot included), the female socket sits in the far end face. Plugged into a port
  with the controller on the panel, the body hangs below the port with its length running
  front to back and the socket facing the back wall; the two bodies touch (the ports are
  12.74 mm apart), so the pair is 25.4 mm wide. POWER is the left one seen from the back.
- **Where they end up** (model, `z_chip` = 0.5): receptacle axis 3.7 mm behind the frame's
  back face; bodies from 1.1 mm in front of that face to 18.2 mm behind it, i.e. 0.5 mm
  short of the cavity back and 2.9 mm inside the outer surface; body underside 0.4 mm
  above the frame's outer wall; the pair spans x = -12.4..13.0 mm.
- **Window:** one 26.2 x 9.2 mm rounded-rectangle opening through the back wall (0.4 mm
  clearance per side in x, 0.6 in y, around the pair), with a 1 mm 45-degree chamfer on the
  outside; the socket faces sit 2.9 mm down in it and a cable plug's boot goes into that
  well. One groove beside the window's right end marks USB, as in v4. One cable at a time
  is the design case (chosen 2026-09-19); two boots at or under the USB-C maximum of
  12.35 mm would also fit side by side.
- **Cradle:** a U on the cavity back around the last 6 mm of the bodies: a 2 mm bottom wall
  merging into the shell's bottom wall and 2 mm side walls up to the bodies' top face,
  0.3 mm clearance in x and 0.5 in y (the boot gap that sets the bodies' height is an
  estimate), 1 mm entry chamfer, open towards the controller. It takes the sideways and
  downward part of the cable forces so they cannot lever the adapters in the receptacles,
  and stops the pair creeping out. The right side wall ends 1.15 mm below the mic1 hole and
  the window's chamfer passes 1.5 mm from it (the mic position is +-1 mm).
- **Frame notch (hand work):** the bodies reach 1.1 mm in front of the frame's back face,
  and right there the frame's back plate has a 3.9 mm strip between its opening (58.4 mm
  from the centre at the ports) and its 1.6 mm outer wall. Cut that strip away over
  27.4 mm centred on the ports, through the plate: in the panel's own orientation (arrows
  up, seen from the back) x = -62.3..-58.4, y = -14.0..+13.4 mm. The outer wall stays.
  `render_frame_notch.png` shows the cut in red below the two ports. On 2026-09-19 an
  adapter on the uncut plate stopped about 1 mm short of seating; after cutting, check
  that both seat fully and that the bodies clear the wall.
- **Removed from v4:** the two sockets with their cut-outs and screw holes, the ribbon
  hooks, and the flange keep-outs (the lower vent band gets its +-30 columns back).
- **Numbers** (printed by the `echo` lines): window 26.24 x 9.2; cradle x = -14.7..15.3,
  z = 12.7..18.7; socket faces z = 18.2; well depth 2.92; pair y = -61.9..-53.9.
- **Assembly change:** push an adapter onto each port before the panel slides in (it only
  goes one way, body towards the frame's edge); the pair enters the cradle as the panel
  seats. Then the six screws as in v1. Cables plug in from the back, straight, 2.9 mm deep
  in the window. Standing, the back face points 6 degrees below horizontal (12 degrees of
  lean minus the 6 degree wedge), and the window's centre is about 8 mm above the table,
  so a cable leaves backwards, slightly downwards, and reaches the table a few
  centimetres behind the display (`render_product_back.png`).
- **Printing:** as v4. The cradle walls stand up from the bed face; the window's chamfer
  is a 45-degree overhang that starts at the bed and needs no support.

### Edges (2026-09-19, edited in place)

Up to here every outer edge of the shell was sharp except the four vertical corners
(2.6 mm radius, the most a uniform 2 mm wall allows against the frame's sharp corners).
On 2026-09-19 the user asked for edges that are softer to the hand, and v6 was edited in
place rather than forked: the envelope (walls, base plane, front, back plane) did not
move, only its edges did. With `edge_back_c`, `edge_rim_r`, `edge_base_r` and
`cb_chamfer` set to 0 the previous body comes back exactly (checked against the previous
`output/v6` mesh: same volume, area and vertex set).

- **Back face perimeter:** a 45-degree chamfer, 1.4 mm (`edge_back_c`). This edge is the
  bed side of the print, where a fillet would start as a horizontal overhang; a chamfer
  prints clean. 1.4 rather than 1.5 because the top wall and the base meet the back face
  at 96 degrees, not 90, which brings the chamfer closer to the cavity: the material left
  on the corner diagonal (cavity edge to chamfer plane) is 2.02 mm at the top edge, 2.12 at
  the sides and 2.36 at the bottom, printed by the `edges:` echo line; 1.5 mm would leave
  1.95 at the top. The chamfer wraps each rounded corner as a cone. On the back face it
  ends 1.6 mm from the USB window's own chamfer and over 4 mm from the nearest counterbore.
  The bed footprint is now 129.6 x 130.3 mm.
- **Front rim:** a 1.5 mm fillet on the outer edge (`edge_rim_r`). The rim is the end of
  the 2 mm wall, so 0.5 mm of flat remains next to the panel; the pocket edge itself stays
  sharp and the fit against the frame is unchanged.
- **Base front edge:** a 3 mm fillet (`edge_base_r`) on the acute 78-degree edge under the
  LED face, the sharpest edge of the shell and the front line of the base. It is backed by
  the solid wedge. The base's front contact line moves back 3.7 mm and the back chamfer
  moves the rear one forward 1.4 mm, so the contact patch is 29.6 mm deep instead of 34.7;
  the centre of gravity stays about 7.6 mm in front of the rear contact line, so a backward
  push of about 7 degrees tips it (8 before). Along the two front-bottom
  corners the 3 mm rounding blends into the rim's 1.5 mm and the vertical corners' 2.6 mm.
- **Counterbores:** a 0.5 mm chamfer on the six mouths (`cb_chamfer`), where the fingers
  land on the back.
- **Unchanged:** pocket, ledge, bosses, cradle, vent slots, pin and mic holes, encoder
  spot-faces (they must stay flat for the nut), and the USB window with its 1 mm chamfer.
- **How it is built:** `outer_body()` is the hull of thin sections of the envelope, each
  with every bounding plane moved inwards by its own amount: a stack of 13 sections
  sweeps the rim fillet (7.5 degrees per facet, like `$fn = 48`), a capsule of two spheres
  makes the base bar, two sections make the back chamfer. Each section keeps a corner
  radius of `r_out` minus its inset, so the vertical corners still come out at 2.6 mm.
- **Printing:** the chamfer rises from the bed at 45 degrees along the two straight sides
  and at 42 degrees along the top and bottom edges (those two walls overhang 6 degrees),
  1.4 mm tall, no support. The fillets are at the top of the print. Elephant-foot
  compensation matters less for the outline now (the first layer is inside the chamfer)
  but keep it for the holes. `render_section_edge.png` shows the profile through x = 33
  (`part = "section_edge"`, a cut clear of every feature).

## v5: speaker in the back

`src/p64_enclosure_v5.scad` is v4 plus the 8 ohm speaker box that ships with the
controller, mounted in the upper half of the back face and firing backwards. The shell's
outline, depth and everything below the panel's centre line are unchanged from v4. It is
kept as an alternative: on 2026-09-11 the user chose to keep v4 as the version to print.

- **Hardware** (hand-measured, see `input/measurements.md` and the photos): a
  99.4 x 44.4 x 20.7 mm box, two drivers on the front face, four corner lugs about
  8.4 x 8.5 mm with 6.0 mm holes, the lugs' back face 12 mm behind the driver face, the
  body continuing 8.7 mm behind that; the lead leaves the box 5.9 mm behind the driver face
  and plugs into the controller's SPK header (JST PH). Hole centres are assumed at the lug
  centres.
- **How it mounts:** a plus-shaped window in the back wall (the 99.4 x 27.5 mm band between
  the side lugs crossed with the 82.6 x 44.4 mm band between the end lugs, 0.5 mm
  clearance) lets the rear 8.7 mm of the body into the cavity; the four lugs rest on the
  outer back face and M4 x 10 thread-forming screws go through them into four printed
  bosses inside (8 mm, 7 mm tall, 3.6 mm pilot, clipped clear of the window). The drivers
  stay exposed and face the wall behind the display; the box stands 12 mm proud of the
  back. No grille needed.
- **Where:** envelope centred at (0, +27): window from y = 4.3 to 49.7, 1.9 mm below the top
  screw bosses; the sunk body ends 2.4 mm behind the panel's VH4 power socket at the
  window's bottom edge and 3.4 mm behind the HUB75 OUT header. Lug hole centres at
  (+-45.5, 9.0) and (+-45.5, 45.0).
- **Lead:** it leaves the box outside the shell, so a 4.5 mm notch in each end edge of the
  window lets it in (either end, so the box can go in either way round). Inside it runs down
  the cavity to the SPK header at the controller's right edge; 150 mm is ample.
- **Vents:** the upper vent band is gone (the speaker covers it); two vertical slots on each
  side of the box (x = +-57.5 and +-61.5, 34 mm long) keep an exhaust at the top. The lower
  band is as in v4.
- **Assembly change:** push the box into the window from behind after the panel is in, with
  the lead's corner at whichever end is convenient, lead through the notch, then the four
  screws. The lead plugs into the SPK header before the shell closes, so leave enough slack
  inside, or plug it through the window before seating the box.
- **Printing:** as v4. The window and the four bosses print without support; the bosses
  grow from the bed side.

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

- Print `output/v1/p64_enclosure_print.stl` (or the same file in `output/v2/` to `output/v6/`) as delivered: it already lies on its inclined back face,
  no supports needed. The walls lean 6 degrees, the base face overhangs 6 degrees, the
  ledge underside is 45 degrees, and there are 6.5 mm bridges over the screw counterbores.
  Print height is 34.6 mm; about 80 g of PLA with 20 percent infill.
  v6 adds the cradle ribs on the bed side and the chamfered window, and since 2026-09-19
  a 1.4 mm chamfer around the bed-side edge (first layer 129.6 x 130.3 mm) with the rim
  and base fillets at the top of the print (print height 34.2 mm, the front-bottom edge
  being rounded); nothing else changes.
- 0.2 mm layers, 3 to 4 perimeters, 20 percent infill, PLA or PETG.
- For FDM use the 0.3 mm `p64_enclosure_print.stl`: the 0.45 mm `_service.stl` printed in
  PLA left the panel with slight side play (see below). Keep the 0.45 mm file for MJF/SLA.
- Enable elephant-foot compensation (0.1 to 0.2 mm) so the screw counterbores and the
  debossed labels stay clean on the first layer.
- Bed needs at least 140 x 140 mm.

## Verified against the Waveshare drawing

`input/RGB-Matrix-P2-64x64-2D.dwg` (identical to Waveshare's GitHub copy) contains
the plastic frame ("JXS-P2-128*128 bottom case") and the LED mask, not the PCB. Checked:

| Item | Drawing | Model |
|---|---|---|
| Six M3 inserts | (0, +-56.85), (+-56.85, +-44), M3, on dia 9.8 faces | same to 0.01 mm; boss dia 10 |
| Frame outline | 127.8 x 127.8, sharp corners, 12 mm deep, no draft | pocket 128.4 (0.3 mm per side), corner r 0.6, lip 12 |
| Outer wall / rim for the ledge | 1.6 mm thick (inner face at +-62.3) | ledge 1.6 mm, sits fully on the wall |
| Back opening | +-51.9, widening to ~+-58.4 for y within +-16 | USB-C ports fall inside the wide part |
| Corner screw recesses | dia 6 at (+-56, +-56), M1 x 6 screws | inside the cavity, 3.7 mm from the ledge |
| Pins | dia 3 x 3 mm at (+-56, -29) | inside the cavity, clear of ledge and bosses |

The controller's own dimensions come from `input/ESP32-S3-RGB-Matrix-2D.pdf` (1:1 vector PDF).

## Things that could not be verified

| Input | Value used | Parameter | Notes |
|---|---|---|---|
| Centre of the panel's HUB75 IN header | (-35.0, +5.4) mm from the panel centre, panel arrows up, seen from the back | `hub75_in_native` | From the product photo, +-1 mm. Pin holes (3.5 mm), mic holes (3.5 mm) and the plug pocket (30 mm) are sized to absorb that error. With this value the chip's edge overhangs the frame rim by 0.8 mm, which fits the photo. v1 print: the base pocket lands exactly on the two USB-C ports; whether the pin and mic holes land over the buttons and mics is not checked yet. |
| Height of the controller PCB's back face above the frame's back face | 0.5 mm | `z_chip` | Must be 0 or more because the chip edge overhangs the rim. Only the plug pocket (`pocket_z`) depends on it. v1 print: with the panel seated there is a visible gap between the controller and the back wall, so the value is safe. |
| Right-angle plug body | up to 12 wide x 7 thick x 14 long | `pocket_w`, `pocket_z` | Enlarge the pocket if yours is bigger. v1 print: not tried yet. v6 has no pocket. |
| 90-degree adapter: boot gap between its body and the receptacle face (v6) | 1.2 mm | `ad_gap` | 7.7 mm plug protrusion minus a 6.5 mm plug shell, from the measured photo. Sets how far below the ports the bodies hang; the cradle has 0.5 mm of clearance in that direction. Measure the seated adapter after notching the frame. |
| 90-degree adapter: corner radius of the body cross-section (v6) | 3.0 mm | `ad_r` | From the photos, looks like 3.5 to 4. A smaller value in the model only makes the cradle and window corners tighter than needed. |

## Verified with the v1 print

The 2026-09-05 order of `output/v1/p64_enclosure_service.stl` (0.45 mm clearance) came back
on 2026-09-18, printed by JLC3DP in black FDM PLA for $18.83 including shipping. Checked
by hand the same day; the hand
measurements are in `input/measurements.md` and the photos are `input/PXL_20260918_*.jpg`
(open shell beside the panel, assembled display in hand, front, back and side standing on
the desk, panel with controller and the right-angle cable, lit at night).

| Item | Result |
|---|---|
| Overall size | 133.0 mm wide (model 132.4, +0.6 mm), 34.6 mm deep at the bottom edge (model 34.6). |
| Screw bosses | All six line up with the panel's inserts. The 3.4 mm holes and 6.5 mm counterbores printed clean; a screw drops in without cleaning. |
| Panel in the pocket | Drops in by hand with slight side play, under 1 mm, at 0.45 mm clearance. Next FDM print: use the 0.3 mm `p64_enclosure_print.stl`. |
| Seating | The frame rests on the ledge all round and the LED mask stands about 2.5 mm proud, as designed. |
| USB-C opening | The base pocket lands exactly on the two ports. |
| Controller clearance | Visible gap between the controller and the back wall with the panel seated. |
| Back face | Groove marks legible, vent slots, pin holes and mic holes clean; the 2 mm minimum-feature rule held. |
| Stand | Stable at the 12 degree lean; the front-bottom bar stays (the v2/v4 choice over v3's plinth, confirmed on the real part). |
| Heat | Barely warm after running for a while; PLA is fine at the brightness used. |
| Not checked yet | Pin holes and mic holes over the buttons and mics (press BOOT through the hole with a pin); the right-angle plug in the base pocket and the cable in the groove to the back notch. |

Screws: none shipped with the panel or the controller. Buy six M3 x 10 (socket head cap or
pan head, head 6 mm or less for the 6.5 mm counterbore; not countersunk). The next shell
(v6) adds no screws: the encoder boards bring their own bushing nut and washer, and the
90-degree adapters simply stay plugged into the controller.

## Main parameters (`src/p64_enclosure.scad`)

- `depth_bottom` 22, `depth_top` 8 (set equal for a flat back), `tilt` 12, `lip` 12,
  `wall` 2, `back_t` 2.4, `panel_clr` 0.3
- `screw_len` 10, `engage` 5, `cb_d` 6.5, `boss_od` 10
- `panel_rot` 90 (set 0 for the un-rotated panel; the cable pocket then no longer applies)
- `vents`, `pin_d`, `mic_d`, `label_size`
- `part` selects `shell`, `print`, `assembly`, `section_x`, `section_y`
- v2 only: `encoders`, `enc_pos`, `enc_rot`, `enc_bush_l`, `enc_spot_t`, `enc_post_d`,
  `enc_peg_d`, `enc_keepout`; `part` also accepts `section_enc`
- v6 only: `adapters`, `ad_*` (the 90-degree adapters, window and cradle); the edge
  treatments `edge_back_c` 1.4, `edge_rim_r` 1.5, `edge_base_r` 3, `cb_chamfer` 0.5 (all 0
  = the sharp-edged body); `part` also accepts `section_ad`, `section_win`, `frame` and
  `section_edge` (cut at `section_edge_x`)

Regenerate the STL with:

```
openscad -o output/v1/p64_enclosure_print.stl -D "part=\"print\"" src/p64_enclosure.scad
openscad -o output/v2/p64_enclosure_print.stl -D "part=\"print\"" src/p64_enclosure_v2.scad
openscad -o output/v2/p64_enclosure_service.stl -D "part=\"print\"" -D panel_clr=0.45 src/p64_enclosure_v2.scad
openscad -o output/v3/p64_enclosure_print.stl -D "part=\"print\"" src/p64_enclosure_v3.scad
openscad -o output/v3/p64_enclosure_service.stl -D "part=\"print\"" -D panel_clr=0.45 src/p64_enclosure_v3.scad
openscad -o output/v4/p64_enclosure_print.stl -D "part=\"print\"" src/p64_enclosure_v4.scad
openscad -o output/v4/p64_enclosure_service.stl -D "part=\"print\"" -D panel_clr=0.45 src/p64_enclosure_v4.scad
openscad -o output/v5/p64_enclosure_print.stl -D "part=\"print\"" src/p64_enclosure_v5.scad
openscad -o output/v5/p64_enclosure_service.stl -D "part=\"print\"" -D panel_clr=0.45 src/p64_enclosure_v5.scad
openscad -o output/v6/p64_enclosure_print.stl -D "part=\"print\"" src/p64_enclosure_v6.scad
openscad -o output/v6/p64_enclosure_service.stl -D "part=\"print\"" -D panel_clr=0.45 src/p64_enclosure_v6.scad
```

The v2 renders are previews (no `--render`) with these cameras, 1600 x 1200, Metallic scheme.
The v3 to v5 renders use the same commands with `v3`, `v4` or `v5` in both paths; v4 adds
`render_section_usb.png` with `part="section_usb"` and `--camera=210,-70,70,-60,-40,5`,
v5 adds `render_section_spk.png` with `part="section_spk"` and `--camera=170,-40,90,-60,27,10`.
v6 has no `section_usb`; instead `render_section_ad.png` uses `part="section_ad"` with
`--camera=110,-120,55,-6,-50,8`, `render_section_win.png` uses `part="section_win"` with
`--camera=15,-180,45,0,-55,10`, `render_frame_notch.png` uses `part="frame"` with
`--projection=o --camera=0,-48,170,0,-48,0` (orthographic, straight at the back of the panel),
and `render_section_edge.png` uses `part="section_edge"` with `--projection=o --camera=333,-4,5,33,-4,5`
(orthographic, looking at the cut face from +X: the base is on the left, the back face on top):

```
openscad -o output/v2/render_back.png        -D "part=\"shell\""       --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-235,-327,307,0,-12,-8 src/p64_enclosure_v2.scad
openscad -o output/v2/render_assembly.png    -D "part=\"assembly\""    --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-235,-327,307,0,-12,-8 src/p64_enclosure_v2.scad
openscad -o output/v2/render_section_enc.png -D "part=\"section_enc\"" --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-252,-48,94,60,0,8 src/p64_enclosure_v2.scad
openscad -o output/v2/render_print.png       -D "part=\"print\""       --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-270,-350,270,0,0,15 src/p64_enclosure_v2.scad
openscad -o output/v2/render_product.png      -D "part=\"product\""    --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=300,495,180,0,0,66 src/p64_enclosure_v2.scad
openscad -o output/v2/render_product_back.png -D "part=\"product\""    --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=308,-440,283,0,0,60 src/p64_enclosure_v2.scad
```

`part = "product"` stands the assembled shell on its wedge foot (front towards +Y, leaning
back 12 degrees, base on z = 0) with a black LED-face mock-up and a table slab.

## Sources

- Panel drawing: https://github.com/waveshareteam/RGB-Matrix-Px-xx/tree/main/hardware/dimensions/RGB-Matrix-Pxx-64x64 (`RGB-Matrix-P2-64x64-2D.dwg`)
- Controller drawing: https://github.com/waveshareteam/ESP32-S3-RGB-Matrix/tree/main/hardware/dimensions (`ESP32-S3-RGB-Matrix-2D.pdf`)
- Wikis: https://docs.waveshare.com/ESP32-S3-RGB-Matrix and https://docs.waveshare.com/RGB-Matrix-Px-64x64
- Encoder board (v2): https://www.adafruit.com/product/5880 and its EagleCAD files
  https://github.com/adafruit/Adafruit-I2C-QT-Rotary-Encoder-PCB
- Encoder (v2): Bourns PEC11 datasheet https://cdn-shop.adafruit.com/datasheets/pec11.pdf
- Controller GPIO socket pinout (v2): the schematic in
  https://github.com/waveshareteam/ESP32-S3-RGB-Matrix/tree/main/hardware/schematics
- Panel-mount USB-C extension (v4): JUXINICE 2-pack, 90 degree, 15 cm, USB 2.0, 100 W PD,
  https://www.amazon.com/JUXINICE-USB-Male-Female-Cable/dp/B0FL7HY5G8 ; dimensions taken
  from the seller's listing drawings.
- 90-degree USB-C adapter (v6): a generic aluminium-shelled male-to-female right-angle
  adapter, no drawing; dimensions hand-measured (`input/measurements.md`).

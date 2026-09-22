# Tabletop enclosure for the Waveshare RGB-Matrix-P2-64x64 + ESP32-S3-RGB-Matrix

This folder is the enclosure part of the [p64 project](../README.md). All paths and
commands below are relative to `enclosure/`.

A single-piece, support-free 3D printed back shell that screws onto the six M3 brass
inserts of the 128 x 128 mm P2 LED matrix, encloses the ESP32-S3-RGB-Matrix controller
that is plugged into the panel's HUB75 IN header, and stands the display on a desk
leaning back 12 degrees. The USB-C cables plug into two small 90-degree adapters that
stay on the controller's ports, through one window in the back face; a second small
print, the cradle insert, is glued inside around them.

The design is **v7**, in one OpenSCAD file, and p64 comes in two variants that share it:

| Variant | Shell | What differs |
|---|---|---|
| **p64a**, solder-less | `src/p64_enclosure_v7a.scad`, outputs in `output/p64a/v7a/` | No rotary encoders: no posts, bushing holes or spot-faces on the back, the lower vent band keeps all its columns. Otherwise identical. |
| **p64b**, with soldering | `src/p64_enclosure_v7b.scad`, outputs in `output/p64b/v7b/` | Two rotary encoders (Adafruit 5880 boards) on the back face, one knob per side; the wiring projects are in `../docs/hardware/`. |

The two variant files are three lines each: they include `src/p64_enclosure_v7.scad`, the
geometry, and set `encoders` to false or true. Both variants are supported for good, so a
change to the shell is made once, in `p64_enclosure_v7.scad`, and both output folders are
regenerated. Nothing of v7 has been printed yet; the fit of the pocket, ledge and bosses,
unchanged since the first version, was verified on the v1 print (see below). The earlier
versions v1 to v6 and their story are in [`archive/pre-v7/`](archive/pre-v7/README.md).

## Files

| Path | What it is |
|---|---|
| `src/p64_enclosure_v7.scad` | The parametric OpenSCAD source (OpenSCAD 2021.01+): the whole shell, the cradle insert, the mock-ups and the section views. Everything below is a parameter. Rendered on its own it gives p64b. |
| `src/p64_enclosure_v7a.scad`, `src/p64_enclosure_v7b.scad` | The variant files: include the source and set `encoders`. Render these, not the source, so the output lands in the right folder. |
| `output/p64a/v7a/`, `output/p64b/v7b/` | One folder per variant and version, identical file names inside: `p64_enclosure_print.stl` / `.3mf` (ready to slice, print orientation), `p64_enclosure_service.stl` (0.45 mm fit clearance instead of 0.3, for MJF/SLA bureaus), `p64_cradle_insert.stl` (the second print), and the renders: `render_back.png`, `render_assembly.png` (with the mock-ups), `render_print.png`, `render_product.png` / `render_product_back.png` (standing on a table), `render_section_ad.png` (through the POWER adapter), `render_section_win.png` (along the adapters' centre line), `render_window.png` (straight at the window: the two socket faces with the insert's sole around them), `render_insert.png` (the insert alone), `render_frame_notch.png` (where to notch the panel frame, in red), `render_section_edge.png` (the wall profile through x = 33: the edge chamfer and fillets); p64b adds `render_section_enc.png` (through an encoder). |
| `input/measurements.md` | Hand measurements of parts that have no drawing: the v1 print, the 90-degree USB-C adapter. |
| `input/usb-c-90-degree-adapter/` | Photos of the 90-degree USB-C adapter; one has the hand measurements drawn on it. |
| `input/PXL_20260918_*.jpg` | Photos of the v1 print, alone and assembled. |
| `input/RGB-Matrix-P2-64x64-2D.dwg`, `.pdf` | Waveshare's drawing of the panel frame (DWG and a PDF rendering of it). |
| `input/ESP32-S3-RGB-Matrix-2D.pdf` | Waveshare's 1:1 drawing of the controller board. |
| `input/*details*.jpg` | Waveshare product photos used for the features the drawings do not cover. |
| `input/adafruit-5880/` | Adafruit's EagleCAD board file of the encoder breakout (CC BY-SA), with a README of what the shell reads from it (p64b). |
| `archive/pre-v7/` | Versions v1 to v6 (sources, outputs, the speaker photos of v5) and their documentation, moved on 2026-09-22. v1 is the only printed one. |
| `archive/2026-08-stand-concept/` | Earlier, abandoned concept (detachable stand, wall keyholes). Kept for reference only. |
| `archive/2026-09-dhruv-solidworks/` | Dhruv's separate SolidWorks take on the enclosure (`p64.SLDPRT` and its STEP export). Kept as-is, never edited here. |

## What the design does

- **Fit:** pocket 128.4 x 128.4 mm (0.3 mm clearance per side around the 127.8 mm frame
  measured in Waveshare's drawing), 2 mm walls, 2.4 mm back wall.
  The walls wrap 13.5 mm forward: over the panel's 12 mm plastic frame and 1.5 mm of the
  2.5 mm LED board and mask stack in front of it, so the mask stands 1.0 mm proud of the
  rim (`proud`; 2.5 mm until 2026-09-22, when the rim ended at the frame) and the front
  is nearly bezel-less. Beside the board the pocket keeps 0.2 mm per side around the
  assumed 128.0 mm board outline (`board`, `board_clr`, not in any drawing); if the real
  board is wider the pocket steps out there automatically and the `front:` echo line
  reports the wall left.
- **Size:** 132.4 mm wide, 132.4 mm tall at the back edge and 139.9 mm at the front (the
  base wedge). The side profile is a wedge: 35.5 mm deep at the bottom (13.5 mm lip +
  22 mm shell) thinning to 21.5 mm at the top (13.5 + 8). The back face is one flat plane
  sloping 6 degrees, so the print still lies flat on it.
- **Interior:** 19.4 mm clear at the bottom edge, 12.9 mm at the top edge of the controller,
  5.8 mm at the top edge of the panel. The mounted controller needs roughly 9 mm and the
  VH4 power plug with its wires about 8 mm, both in the deep half.
- **Mounting:** six counterbored bosses on the panel's inserts, positions from Waveshare's
  drawing: (0, +-56.85) and (+-56.85, +-44) mm, rotated with the panel. Bosses are 10.5 mm in
  diameter around a 6.5 mm counterbore (2 mm walls). M3 x 10 screws go in
  from the back; the boss floor is 5 mm so about 5 mm of thread engages the insert. The two
  top bosses are only 6.6 mm tall because of the wedge; their screw heads end up 1 mm below
  the back face (keep `depth_top` at 8 mm or more, or use M3 x 8 there).
  A 1.6 mm seating ledge with a 45-degree underside supports the frame's 1.6 mm outer wall
  all around, except over 30 mm below the USB-C ports where it is relieved (see the window).
- **Tilt / stand:** the bottom wall thickens into a wedge so the whole base is one flat plane
  at 12 degrees. With the edge fillets the contact patch is 31.2 mm deep; the centre of
  gravity lands about 8 mm in front of the rear contact line, so it takes roughly a
  7-degree backward push to tip.
- **Panel orientation:** the panel is installed rotated 90 degrees counter-clockwise (seen
  from the back) so the controller sits at the bottom with its USB-C ports facing down.
  The LED image is rotated 90 degrees in firmware.
- **Cables:** two small 90-degree USB-C adapters stay plugged on the controller's POWER and
  USB ports; their sockets face the back wall through one window at the bottom centre of
  the back face, and the external cables plug in from behind. Nothing hangs below the base.
- **Back wall:** two bands of ventilation slots, 3 mm pin holes over BOOT and RESET marked
  with short grooves (one groove = BOOT, two grooves = RESET; text labels were dropped because
  their 0.3 mm ribs are too thin for bureau printing), 3.5 mm holes over the two microphones,
  six screw counterbores, the USB-C window with one groove beside its right end (= USB, the
  programming port; POWER has none), and in p64b the two encoder bushings with their
  spot-faces. Every wall and rib is 2.0 mm or thicker, with two named exceptions in the
  glued insert.
- **Edges:** the back face's perimeter has a 1.4 mm 45-degree chamfer, the front rim a
  1.5 mm fillet, the base's front edge a 3 mm fillet, the counterbore mouths a 0.5 mm
  chamfer (see Edges).

Not included: access to the TF card slot. In every orientation the card slot ends up about
39 mm from the nearest wall, so a slot in the shell would be useless. The card has to be
inserted before closing the shell.

## USB-C: the adapters, the window and the cradle insert

- **Hardware** (hand-measured on 2026-09-19, `input/measurements.md`, photos in
  `input/usb-c-90-degree-adapter/`): a generic 19.3 x 12.7 x 8.0 mm aluminium-shelled
  male-to-female right-angle USB-C adapter; the male plug leaves the wide face with its
  centre 4.8 mm from one end (7.7 mm proud of the face, boot included), the female socket
  sits in the far end face. Plugged into a port with the controller on the panel, the body
  hangs below the port with its length running front to back and the socket facing the
  back wall; the two bodies touch (the ports are 12.74 mm apart), so the pair is 25.4 mm
  wide. POWER is the left one seen from the back. Both adapters fit at once (checked
  2026-09-19).
- **Where they end up** (model, `z_chip` = 0.5): receptacle axis 3.7 mm behind the frame's
  back face; bodies from 1.1 mm in front of that face to 18.2 mm behind it, i.e. 0.5 mm
  short of the cavity back and 2.9 mm inside the outer surface; body underside 0.41 mm
  above the frame's outer wall; the pair spans x = -12.4..13.0 mm, y = -61.9..-53.9.
- **Window:** one 28.3 x 10.7 mm rounded-rectangle opening through the back wall, corner
  radius 3.0 (the body's own), 0.5 mm 45-degree chamfer outside. It is sized by the
  tolerance budget below: 1.45 mm of clearance per side in x, 1.95 mm above the bodies and
  0.76 mm below them, so its centre sits 0.6 mm above the pair's. The socket faces sit
  2.9 mm down in it and a cable plug's boot goes into that well. One cable at a time is the
  design case; two boots at or under the USB-C maximum of 12.35 mm would also fit side by
  side. The mic1 hole sits 0.5 mm right and 1.6 mm up from where the microphone is
  (`mic1_off`) so the webs around it keep 2 mm against the window: 2.18 mm to the window
  at the inner face (the chamfer outline comes 1.68 mm close on the surface), 2.05 mm to
  the nearest vent slot. The microphone is about 10 mm behind the wall, so the hole is a
  sound vent and the offset changes nothing audible.
- **Frame notch (hand work):** the bodies reach 1.1 mm in front of the frame's back face,
  and right there the frame's back plate has a 3.9 mm strip between its opening (58.4 mm
  from the centre at the ports) and its 1.6 mm outer wall. Cut that strip away through the
  plate, 27.4 mm wide centred on the **real** ports (1 mm each side of the pair); the outer
  wall stays. Measured from the frame instead of the ports, the whole position budget needs
  29.4 mm: in the panel's own orientation (arrows up, seen from the back) x = -62.3..-58.4,
  y = -15.0..+14.4 mm, which is what `render_frame_notch.png` shows in red. On 2026-09-19
  an adapter on the uncut plate stopped about 1 mm short of seating; after cutting, check
  that both seat fully and that the bodies clear the wall (that check also verifies the
  downward side of the budget, see below).
- **Cradle insert** (`part = "insert"`, `p64_cradle_insert.stl`): a 34 x 7 x 6 mm second
  print: a U (1.2 mm bottom wall, 2 mm side walls up to the bodies' top face, open towards
  the controller) around the last 6 mm of the bodies, standing on a 1.2 mm sole that lies
  on the cavity back and reaches 3 mm beyond the window on each side and 1 mm below it.
  Its opening has 0.55 mm of clearance in x and 0.75 in y (0.3/0.5 plus the print
  allowance), because it is glued to the cavity back while sitting on the real adapters and
  so aligns itself to wherever they are. It takes the sideways and downward part of the
  cable forces so they cannot lever the adapters in the receptacles. The bodies pass
  through the sole's opening into the wall's window, so from outside the sole frames the
  two socket faces at 2.4 mm depth and hides the shell's wider opening on the sides and
  below; above the bodies the 1.95 mm strip stays open into the cavity (a bar there would
  sit in front of the mic1 hole at the worst case). Two 1.5 x 1.2 mm rails on the cavity
  back bracket the sole with the whole x budget of play: a guide for placing it and a shear
  key for the glue. The 1.2 mm sole and bottom wall are deliberate exceptions to the 2 mm
  rule: the sole is a glued lamination on the 2.4 mm wall, the bottom wall is backed by
  the shell's bottom wall within 0.36 mm (a 0.05 mm overlap only if the bodies sat as low
  as they physically can, which would just lift the glued insert by that much; that is
  why it is 1.2 and not 2). 1.2 mm is JLC3DP's thin-wall line: at 1.0 their check flagged
  the insert on 2026-09-22, and a 1 mm entry chamfer along the bottom wall tapered it to
  a knife edge. The entry chamfer is now 0.8 mm on the side walls only (1.2 mm left at
  the edge); the bottom wall has none. The wall's own 2.4 mm depth keeps the
  plugs from ever leaving the receptacles: a plug shell is 6.5 mm long.
- **Ledge relief:** the seating ledge is cut away over 30.3 mm centred below the ports
  (`ledge_relief`); in the previous design the bodies passed 0.7 mm above it.
- **Standing:** the back face points 6 degrees below horizontal (12 degrees of lean minus
  the 6 degree wedge) and the window's centre is about 8 mm above the table, so a cable
  leaves backwards, slightly downwards, and reaches the table a few centimetres behind
  the display (`render_product_back.png`).
- **Numbers** (the `budget:`, `adapters:`, `insert:`, `webs:` and `ledge relief:` echo
  lines): window 28.34 x 10.71, centre y -57.3, bottom edge y -62.65; insert opening
  26.54 x 9.5, outer width 30.54, sole 34.34 x 7.25, rails at x -20.1..-18.6 and
  19.2..20.7; at the worst case (bodies 1.7 mm higher) the side walls end at y -52.2, 1.6 mm
  below the mic1 hole's edge; the window's chamfer stops 1.95 mm from the back face's
  bottom edge chamfer; socket faces z 18.2, well depth 2.92.

### The tolerance budget

The window and the rails are sized so that the print fits at the first try even if the
inputs the model cannot verify are off by their rated error (decided 2026-09-22: fit
before looks, the position budget as rated, JLC3DP FDM). Every clearance is derived from
these parameters, so the numbers can shrink once the real positions are measured.

| Input | Rated error | Where it comes from | Moves the adapter pair |
|---|---|---|---|
| Port position (`hub75_in_native`, `tol_pos`) | +-1.0 mm in x and y | read off a product photo; the v1 print only showed the ports inside a 30 mm pocket | x and y |
| Boot gap (`ad_gap` 1.2, `tol_gap`) | +-0.5 mm | an estimate (7.7 mm plug minus a 6.5 mm shell), not measured | y (how far the bodies hang below the ports) |
| Body size (`ad_len`, `ad_w`, `ad_t`, `tol_body`) | +-0.2 mm per side of the pair | hand measurements on a photo | x and y edges, z (socket face) |
| Socket face depth (`z_chip` 0..1, `ad_plug_c`, `ad_len`, `tol_z`) | +-1.1 mm | `z_chip` is a range, the other two are photo measurements | z only: the well is 1.8 to 4.0 mm deep instead of 2.9, the plugs stay 6.5 mm in the receptacles |
| FDM print allowance (`print_clr`) | 0.25 mm per side of an opening | a guess from the v1 print, whose outside came out 0.3 mm per side larger than the model | shrinks every opening |
| Body corner radius (`ad_r`, 3.0) | real 3.5 to 4 | photos | only ever adds clearance |

Window clearance: x = 1.0 + 0.2 + 0.25 = 1.45 per side; up = 1.0 + 0.5 + 0.2 + 0.25 =
1.95; down = 0.41 + 0.25 + 0.1 = 0.76, because one bound is physical: the bodies cannot
hang lower than the frame's outer wall (inner face at y = -62.3, from the drawing), or the
adapters would not seat on the ports at all. So downwards the shell needs only the print
allowance beyond those 0.41 mm, and "do both adapters seat fully after the notch is cut"
verifies the whole downward side for free. Not absorbed: a port lower than the frame's
wall allows (a hardware collision, found at that check); a socket depth beyond +-1.1 mm
(only the well's depth changes, the cable still plugs in); the microphone position itself
(+-1 mm from the same photo, irrelevant for a vent hole).

Before v7 the window had 0.4/0.6 mm of clearance per side and the cradle 0.3/0.5, i.e.
0.15/0.35 and 0.05/0.25 after the print allowance: a 1 mm error in the port position put
an adapter body into the wall (`archive/pre-v7/`, v6).

### Shrinking the budget (measure later)

Once the notch is cut and both adapters are seated on the real panel, three caliper
measurements pin the inputs down; after them `tol_pos` can drop to the residual (0.3 mm
is realistic for calipers) and the window shrinks accordingly, the insert unchanged:

| Measure | Nominal | Pins down |
|---|---|---|
| Frame's outer side face to the pair's outer side face, on each side (design orientation, ports down) | 50.9 right, 51.5 left | port x (`hub75_in_native[1]` before the 90 degree rotation) |
| Frame's outer bottom face to the bodies' underside | 2.0 | port y plus the boot gap (`hub75_in_native[0]` and `ad_gap`) |
| Frame's back face to the socket faces (depth gauge) | 18.2 | `z_chip` + `ad_plug_c` + `ad_len` |

Enter what differs, regenerate both variants, and read the echo lines: the model prints
every margin.

## Encoders (p64b only)

- **Hardware assumed:** two Adafruit 5880 boards (I2C "seesaw" rotary encoder breakout:
  25.4 x 25.4 mm PCB with four 2.5 mm plated holes on a 20.32 mm square, a Bourns
  PEC11-style 24-detent encoder with push switch soldered at the centre, 15 mm D-shaft,
  M7 x 0.75 bushing, 6.5 mm body height) and 20 mm set-screw knobs. Checked on
  2026-09-22 against the sources and, the same day, with calipers on one of the user's
  boards (`input/measurements.md`: shaft 15.0, body 6.5, PCB 1.6, holes 2.5, board 25.4,
  washer plus nut 2.5, all as modelled; the bushing measured 6.0 instead of the
  datasheet's 5.0 and the model now uses 6.0, which only changes how far it stands out;
  the pin stubs under the board reach 3.0 mm into the 4.1 mm available there; the knob
  is 20 x 15.5). Against the sources: the board outline, corner radius, hole size and hole
  square come from Adafruit's EagleCAD board file, stored in `input/adafruit-5880/` with
  the numbers read from it; the encoder's body height (6.5 mm from the mounting surface),
  the 12.5 x 13.2 mm body, the M7 x 0.75 bushing, the 6.0 mm shaft with its 4.5 mm flat
  and the bushing length per shaft length (5.0 mm for the 15 mm shaft, 7.0 for 20 mm and
  longer) come from the Bourns PEC11 datasheet (rev. 05/11, cited under Sources). All of
  them match the model. Not measured yet: the washer's outer diameter and the nut across
  its corners, which must fit the 14 mm spot-face (an M7 nut and washer are about 11 to
  12 mm).
- **Where:** the shafts leave the back face at (+-47, -25) mm in the shell's coordinates,
  i.e. 19 mm in from each side edge and 41 mm up from the bottom edge, one knob per side.
  That spot is clear of the (+-56.85, 0) and (+-44, -56.85) bosses and the controller
  (x = -16..26), and needs no extra shell depth.
- **How they mount:** each board lies parallel to the back face, component side towards the
  wall. Four 4.5 mm posts hang from the cavity back and end in 2.2 mm pegs that enter the
  board's holes, so the board cannot turn. The bushing passes through a 7.4 mm hole; a
  14 mm, 0.4 mm-deep spot-face on the outside gives the supplied washer and nut a flat seat
  on the sloping wall and 4.0 mm of thread (the 6.0 mm bushing stands 3.6 mm proud of the
  face).
  The nut takes the knob's push force; the pegs only key the board.
- **Numbers** (printed by the `echo` lines): cavity 15.2 mm deep at the encoder; board
  bottom 7.2 mm behind the frame back face (ledge top is at 3.1 mm); posts 8.3 mm from the
  nearest boss; board edge 4.5 mm from the side wall; shaft 12.6 mm proud of the face; knob
  end 35 mm behind the frame back face with the user's 15.5 mm knob. The knobs are
  therefore the deepest point of the shell, 13 mm past the bottom edge: the display stands
  exactly as without them, but laid on its back it rests on the knobs.
- **Vents:** the lower band loses its outer two columns on each side (x = +-36 and +-42)
  where the boards sit; p64a keeps them.
- **Board orientation:** the side wall is only 4.5 mm from the board's outer edge, so turn
  each board with its two STEMMA QT sockets facing up and down and the header pads towards
  the centre (`enc_rot`). The cables then run vertically past the boards.
- **Wiring (not part of the shell):** the two boards chain on the controller's 4-pin SH1.0
  "GPIO" socket (pin 1 = IO45, 2 = IO46, 3 = 3V3, 4 = GND) used as a second I2C bus. The
  STEMMA QT pin order differs, so use an SH-to-header cable pair rather than a straight
  SH-SH cable; IO45/IO46 carry 10 k pull-downs on the controller, so add 2.2 k pull-ups to
  3V3 on SDA and SCL. Second board: close jumper A0 (address 0x37).
  The complete wiring projects, with schematics, bench procedure and firmware plan, are
  `../docs/hardware/encoders-solderless.md` (one encoder, no soldering) and
  `../docs/hardware/encoders-soldered.md` (two encoders, soldered); both belong to p64b.
- **p64a:** `encoders = false` removes the posts, the bushing holes and the spot-faces and
  gives the lower vent band its four outer columns back; nothing else moves.

## Edges

Every outer edge is softened for the hand; the pocket and the inner walls stay sharp for
an accurate fit. With `edge_back_c`, `edge_rim_r`, `edge_base_r` and `cb_chamfer` set to
0 the sharp-edged body comes back exactly.

- **Back face perimeter:** a 45-degree chamfer, 1.4 mm (`edge_back_c`). This edge is the
  bed side of the print, where a fillet would start as a horizontal overhang; a chamfer
  prints clean. 1.4 rather than 1.5 because the top wall and the base meet the back face
  at 96 degrees, not 90, which brings the chamfer closer to the cavity: the material left
  on the corner diagonal (cavity edge to chamfer plane) is 2.02 mm at the top edge, 2.12 at
  the sides and 2.36 at the bottom, printed by the `edges:` echo line; 1.5 mm would leave
  1.95 at the top. The chamfer wraps each rounded corner as a cone. On the back face it
  ends 1.95 mm from the USB window's own chamfer and over 4 mm from the nearest counterbore.
  The bed footprint is 129.6 x 130.3 mm.
- **Front rim:** a 1.5 mm fillet on the outer edge (`edge_rim_r`). The rim is the end of
  the 2 mm wall, so 0.5 mm of flat remains next to the panel; the pocket edge itself stays
  sharp and the fit against the frame is unchanged.
- **Base front edge:** a 3 mm fillet (`edge_base_r`) on the acute 78-degree edge under the
  LED face, the sharpest edge of the shell and the front line of the base. It is backed by
  the solid wedge. The base's front contact line moves back 3.7 mm and the back chamfer
  moves the rear one forward 1.4 mm, so the contact patch is 31.2 mm deep instead of 36.3;
  the centre of gravity stays about 8 mm in front of the rear contact line, so a backward
  push of about 7 degrees tips it. Along the two front-bottom corners the 3 mm rounding
  blends into the rim's 1.5 mm and the vertical corners' 2.6 mm (the most a uniform 2 mm
  wall allows against the frame's sharp corners).
- **Counterbores:** a 0.5 mm chamfer on the six mouths (`cb_chamfer`), where the fingers
  land on the back.
- **Flat by design:** the encoder spot-faces (they must stay flat for the nut).
- **How it is built:** `outer_body()` is the hull of thin sections of the envelope, each
  with every bounding plane moved inwards by its own amount: a stack of 13 sections
  sweeps the rim fillet (7.5 degrees per facet, like `$fn = 48`), a capsule of two spheres
  makes the base bar, two sections make the back chamfer. Each section keeps a corner
  radius of `r_out` minus its inset, so the vertical corners still come out at 2.6 mm.
- **Printing:** the chamfer rises from the bed at 45 degrees along the two straight sides
  and at 42 degrees along the top and bottom edges (those two walls overhang 6 degrees),
  1.4 mm tall, no support. The fillets are at the top of the print. Elephant-foot
  compensation matters less for the outline (the first layer is inside the chamfer) but
  keep it for the holes. `render_section_edge.png` shows the profile through x = 33
  (`part = "section_edge"`, a cut clear of every feature).

## Assembly

Before the first assembly, once:

1. Cut the frame notch (see the window section), push an adapter onto each port (it only
   goes one way, body towards the frame's edge) and check that both seat fully with their
   bodies clear of the frame's wall.
2. Dry-fit: panel with adapters into the shell, the bodies through the insert's U and the
   insert between the two rails, no glue. Check that the insert lies flat on the cavity
   back with the sole's arms overlapping the wall beside the window.
3. Put a thin bead of cyanoacrylate on the sole's back face along its outer edge only (or
   a strip of double-sided tape), well away from the opening, seat the panel with the
   adapters carrying the insert, fit two screws, and let it cure. The adapters position the
   insert; the shell only receives it. From then on the bodies slide into the U as the
   panel seats.

Every time:

1. Plug the controller onto the panel's HUB75 IN header and wire its 5V/GND screw terminals
   to the panel's VH4 power socket, exactly as in Waveshare's photo (ignore the speaker).
   Push the two adapters onto the ports.
2. p64b: plug the STEMMA QT cables into both encoder boards; from inside the shell push each
   board onto its four pegs with the shaft through the wall; from outside fit the washer
   and nut (hand-tight, 10 kgf.cm max) and then the knob; connect the chain to the
   controller's GPIO socket.
3. Slide the panel into the shell from the front with the controller at the **bottom**
   (USB-C ports pointing at the wedge). It seats on the ledge and the six bosses; the
   adapter bodies enter the insert.
4. Fit six M3 x 10 screws from the back. The heads sit 17 mm deep, so use a long PH1 or
   hex driver.
5. Plug the cables into the adapters through the back window, straight, 2.9 mm deep: POWER
   (power only) on the left seen from the back, USB (power + programming) on the right,
   marked by the groove.

Screws: none shipped with the panel or the controller. Buy six M3 x 10 (socket head cap or
pan head, head 6 mm or less for the 6.5 mm counterbore; not countersunk). Nothing else is
screwed: the encoder boards bring their own bushing nut and washer, the adapters simply
stay plugged in, and the insert takes a drop of cyanoacrylate or a strip of tape.

## Printing

- Print `p64_enclosure_print.stl` from your variant's folder as delivered: it already lies
  on its inclined back face, no supports needed. The walls lean 6 degrees, the base face
  overhangs 6 degrees, the ledge underside is 45 degrees, there are 6.5 mm bridges over the
  screw counterbores, the 1.4 mm edge chamfer rises from the bed (first layer
  129.6 x 130.3 mm), the two rails and (p64b) the encoder posts stand up from the cavity
  back, and the window's chamfer is a 45-degree overhang that starts at the bed. Print
  height 35.7 mm; about 82 g of PLA with 20 percent infill.
- Print `p64_cradle_insert.stl` too: sole down, no support, under 2 g; its walls lean
  6 degrees (they follow the adapter bodies, the sole follows the sloping cavity back). If
  the opening comes out tight, a file pass fixes it; loose is fine.
- 0.2 mm layers, 3 to 4 perimeters, 20 percent infill, PLA or PETG.
- For FDM use the 0.3 mm `p64_enclosure_print.stl`: the 0.45 mm `_service.stl` printed in
  PLA left the panel with slight side play (see below). Keep the 0.45 mm file for MJF/SLA.
- Enable elephant-foot compensation (0.1 to 0.2 mm) so the screw counterbores and the
  grooves stay clean on the first layer.
- p64b: the pegs are 2.2 mm pins; MJF nylon prints them fine, for FDM check the fit and
  sand if needed. The spot-faces sit on the bed face and become a 3.3 mm-wide bridge ring
  at the third layer, which is harmless.
- Bed needs at least 140 x 140 mm.

## Verified against the Waveshare drawing

`input/RGB-Matrix-P2-64x64-2D.dwg` (identical to Waveshare's GitHub copy) contains
the plastic frame ("JXS-P2-128*128 bottom case") and the LED mask, not the PCB. Checked:

| Item | Drawing | Model |
|---|---|---|
| Six M3 inserts | (0, +-56.85), (+-56.85, +-44), M3, on dia 9.8 faces | same to 0.01 mm; boss dia 10.5 |
| Frame outline | 127.8 x 127.8, sharp corners, 12 mm deep, no draft | pocket 128.4 (0.3 mm per side), corner r 0.6, lip 12 |
| Outer wall / rim for the ledge | 1.6 mm thick (inner face at +-62.3) | ledge 1.6 mm, sits fully on the wall; the wall is the physical bound of the adapter bodies |
| Back opening | +-51.9, widening to ~+-58.4 for y within +-16 | USB-C ports fall inside the wide part; the frame notch cuts the plate strip between 58.4 and the wall |
| Corner screw recesses | dia 6 at (+-56, +-56), M1 x 6 screws | inside the cavity, 3.7 mm from the ledge |
| Pins | dia 3 x 3 mm at (+-56, -29) | inside the cavity, clear of ledge and bosses |

The controller's own dimensions come from `input/ESP32-S3-RGB-Matrix-2D.pdf` (1:1 vector PDF).

## Things that could not be verified

| Input | Value used | Parameter | Notes |
|---|---|---|---|
| Centre of the panel's HUB75 IN header | (-35.0, +5.4) mm from the panel centre, panel arrows up, seen from the back | `hub75_in_native` | From the product photo, +-1 mm. The pin holes (3 mm), mic holes (3.5 mm) and the USB-C window (`tol_pos`) are sized to absorb that error. With this value the chip's edge overhangs the frame rim by 0.8 mm, which fits the photo. v1 print: its 30 mm base pocket landed exactly on the two USB-C ports; whether the pin and mic holes land over the buttons and mics is not checked yet. |
| Height of the controller PCB's back face above the frame's back face | 0.5 mm | `z_chip` | Must be 0 or more because the chip edge overhangs the rim; 0 to 1 mm is budgeted (`tol_z`). v1 print: with the panel seated there is a visible gap between the controller and the back wall, so the value is safe. |
| 90-degree adapter: boot gap between its body and the receptacle face | 1.2 mm | `ad_gap` | 7.7 mm plug protrusion minus a 6.5 mm plug shell, from the measured photo. Sets how far below the ports the bodies hang; +-0.5 mm is budgeted (`tol_gap`). Measure the seated adapter after notching the frame. |
| 90-degree adapter: corner radius of the body cross-section | 3.0 mm | `ad_r` | From the photos, looks like 3.5 to 4. A smaller value in the model only makes the insert's and the window's corners tighter than needed. |
| FDM print allowance | 0.25 mm per side of an opening | `print_clr` | A guess from the v1 print (outside +0.3 mm per side, holes "clean"). Measure the v7 window against the model when it arrives. |
| LED board / mask outline | 128.0 mm | `board` | Waveshare's quoted panel size; the drawing has the frame (127.8) and not the board. The rim runs 1.5 mm beside the board with 0.2 mm clearance per side (`board_clr`). Measure the board and the mask at their widest; if over 128.0, set `board` and the pocket steps out (the wall there drops below 2 mm past 128.0, the echo warns). |
| LED board + mask thickness in front of the frame | 2.5 mm | `panel_stack` | From the v1 print, where the mask stood 2.5 mm proud of a rim that ended at the frame. Sets the lip together with `proud`. |
| Encoder washer outer diameter and nut across corners (p64b) | under 14 mm | `enc_spot_d` | Not measured yet; the 14 mm spot-face must take both. Everything else about the 5880 was measured on 2026-09-22 (`input/measurements.md`). |

## Verified with the v1 print

The pocket, ledge, bosses, wedge, vents and pin/mic holes of v7 are those of v1, the
first version, whose 0.45 mm-clearance file (`archive/pre-v7/output/v1/p64_enclosure_service.stl`)
was printed by JLC3DP in black FDM PLA (order of 2026-09-05, received 2026-09-18, $18.83
including shipping). Checked by hand the same day; measurements in `input/measurements.md`,
photos `input/PXL_20260918_*.jpg`.

| Item | Result |
|---|---|
| Overall size | 133.0 mm wide (model 132.4, +0.6 mm), 34.6 mm deep at the bottom edge (model 34.6). |
| Screw bosses | All six line up with the panel's inserts. The 3.4 mm holes and 6.5 mm counterbores printed clean; a screw drops in without cleaning. |
| Panel in the pocket | Drops in by hand with slight side play, under 1 mm, at 0.45 mm clearance. Next FDM print: use the 0.3 mm `p64_enclosure_print.stl`. |
| Seating | The frame rests on the ledge all round and the LED mask stands about 2.5 mm proud of v1's rim, which ended at the frame (v7's rim runs 1.5 mm further, so the mask stands 1.0 mm proud). |
| USB-C ports | v1's 30 mm base pocket landed exactly on the two ports (v7 has a window in the back instead). |
| Controller clearance | Visible gap between the controller and the back wall with the panel seated. |
| Back face | Groove marks legible, vent slots, pin holes and mic holes clean; the 2 mm minimum-feature rule held. |
| Stand | Stable at the 12 degree lean; the front-bottom bar was confirmed on the real part. |
| Heat | Barely warm after running for a while; PLA is fine at the brightness used. |
| Not checked yet | Pin holes and mic holes over the buttons and mics (press BOOT through the hole with a pin). |

## Main parameters (`src/p64_enclosure_v7.scad`)

- Shell: `depth_bottom` 22, `depth_top` 8 (set equal for a flat back), `tilt` 12, `proud`
  1.0 with `panel_stack` 2.5 and `frame_d` 12 (the lip, 13.5, is derived), `board` 128.0,
  `board_clr` 0.2, `wall` 2, `back_t` 2.4, `panel_clr` 0.3; screws `screw_len` 10, `engage` 5, `cb_d` 6.5,
  `boss_od` 10.5; `panel_rot` 90; back wall `vents`, `pin_d` 3, `mic_d` 3.5, `mic1_off`
  [0.5, 1.6]
- Edges: `edge_back_c` 1.4, `edge_rim_r` 1.5, `edge_base_r` 3, `cb_chamfer` 0.5 (all 0 =
  the sharp-edged body)
- Adapters and window: `adapters`, `ad_*` (the measured adapter), `ad_win_chamfer` 0.5,
  `ad_mark`; the budget `tol_pos` [1, 1], `tol_gap` 0.5, `tol_body` 0.2, `tol_z` 1.1,
  `print_clr` 0.25, `win_slack` 0.1; `ledge_relief`
- Insert: `ins_clr` [0.3, 0.5], `ins_depth` 6, `ins_sole_t` 1, `ins_wall_t` 2, `ins_bot_t`
  1, `ins_lap` 3, `ins_top_cut` 2.5, `ins_rails`, `ins_rail_h` 1.2, `ins_rail_t` 1.5
- Encoders: `encoders` (the variant switch), `enc_pos`, `enc_rot`, `enc_bush_l`,
  `enc_spot_t`, `enc_post_d`, `enc_peg_d`, `enc_keepout`
- `part` selects `shell`, `print`, `assembly`, `section_ad`, `section_win`, `section_y`,
  `section_enc`, `section_edge` (cut at `section_edge_x`), `product`, `frame`, `insert`

## Regenerating the outputs

Render the variant files, never the source directly, so the output lands in its folder.
p64b (`v7b`) below; p64a is the same with `p64a/v7a` and `_v7a.scad`, minus
`render_section_enc.png`:

```
openscad -o output/p64b/v7b/p64_enclosure_print.stl   -D "part=\"print\""  src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/p64_enclosure_print.3mf   -D "part=\"print\""  src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/p64_enclosure_service.stl -D "part=\"print\""  -D panel_clr=0.45 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/p64_cradle_insert.stl     -D "part=\"insert\"" src/p64_enclosure_v7b.scad
```

The renders are previews (no `--render`), 1600 x 1200, Metallic scheme:

```
openscad -o output/p64b/v7b/render_back.png         -D "part=\"shell\""        --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-235,-327,307,0,-12,-8 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_assembly.png     -D "part=\"assembly\""     --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-235,-327,307,0,-12,-8 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_section_enc.png  -D "part=\"section_enc\""  --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-252,-48,94,60,0,8 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_print.png        -D "part=\"print\""        --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=-270,-350,270,0,0,15 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_product.png      -D "part=\"product\""      --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=300,495,180,0,0,66 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_product_back.png -D "part=\"product\""      --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=308,-440,283,0,0,60 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_section_ad.png   -D "part=\"section_ad\""   --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=110,-120,55,-6,-50,8 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_section_win.png  -D "part=\"section_win\""  --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=15,-180,45,0,-55,10 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_frame_notch.png  -D "part=\"frame\""        --imgsize=1600,1200 --projection=o --colorscheme=Metallic --camera=0,-48,170,0,-48,0 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_section_edge.png -D "part=\"section_edge\"" --imgsize=1600,1200 --projection=o --colorscheme=Metallic --camera=333,-4,5,33,-4,5 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_insert.png       -D "part=\"insert\""       --imgsize=1600,1200 --projection=p --colorscheme=Metallic --camera=40,-60,50,0,0,2 src/p64_enclosure_v7b.scad
openscad -o output/p64b/v7b/render_window.png       -D "part=\"assembly\""     --imgsize=1600,1200 --projection=o --colorscheme=Metallic --camera=0.3,-57,21,-6,0,0,60 src/p64_enclosure_v7b.scad
```

`render_frame_notch.png` looks straight at the back of the panel (orthographic);
`render_section_edge.png` looks at the cut face from +X (the base on the left, the back
face on top); `render_window.png` uses the gimbal camera form, straight at the back face
around the window, the mic1 hole at the top right. `part = "product"` stands the assembled
shell on its wedge foot (front towards +Y, leaning back 12 degrees, base on z = 0) with a
black LED-face mock-up and a table slab. The `echo` lines of every render print the
margins named in this README.

## Sources

- Panel drawing: https://github.com/waveshareteam/RGB-Matrix-Px-xx/tree/main/hardware/dimensions/RGB-Matrix-Pxx-64x64 (`RGB-Matrix-P2-64x64-2D.dwg`)
- Controller drawing: https://github.com/waveshareteam/ESP32-S3-RGB-Matrix/tree/main/hardware/dimensions (`ESP32-S3-RGB-Matrix-2D.pdf`)
- Wikis: https://docs.waveshare.com/ESP32-S3-RGB-Matrix and https://docs.waveshare.com/RGB-Matrix-Px-64x64
- Encoder board (p64b): https://www.adafruit.com/product/5880 and its EagleCAD files
  https://github.com/adafruit/Adafruit-I2C-QT-Rotary-Encoder-PCB (the board file is
  stored in `input/adafruit-5880/`, CC BY-SA)
- Encoder (p64b): Bourns PEC11 datasheet, rev. 05/11, https://cdn-shop.adafruit.com/datasheets/pec11.pdf
  (not stored: Bourns' copyright)
- Controller GPIO socket pinout (p64b): the schematic in
  https://github.com/waveshareteam/ESP32-S3-RGB-Matrix/tree/main/hardware/schematics
- 90-degree USB-C adapter: a generic aluminium-shelled male-to-female right-angle adapter,
  no drawing; dimensions hand-measured (`input/measurements.md`).

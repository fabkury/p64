// =====================================================================
//  p64_enclosure_v7.scad
//  Tabletop back shell for the Waveshare RGB-Matrix-P2-64x64-B
//  (128 x 128 mm LED matrix, 6x M3 brass inserts) with the
//  ESP32-S3-RGB-Matrix controller plugged into the HUB75 IN header.
//  Needs OpenSCAD 2021.01 or newer.
//
//  v2 (2026-09-10): adds two rotary encoders on the back face, one near
//  each side edge in the lower half. Each is an Adafruit 5880 board
//  (I2C seesaw breakout, 25.4 mm square, Bourns PEC11-style encoder
//  soldered at its centre, 15 mm D-shaft, M7 x 5 mm bushing). The
//  board lies parallel to the back face, its shaft goes straight out
//  through the wall, the encoder's own washer and nut clamp it from the
//  outside under the knob, and four printed posts with pegs in the
//  board's 2.5 mm holes stop it from turning. Everything else is v1.
//
//  v4 (2026-09-11): two JUXINICE panel-mount USB-C extensions screwed to
//  the back face, their L-plugs inside the cavity below the controller's
//  ports, so v2's plug pocket, base groove and back notch are gone.
//  Superseded: the v1 print showed there is no room for them.
//
//  v6 (2026-09-19): the external cables plug into two small 90-degree
//  USB-C adapters that stay on the controller's ports for good. Each
//  adapter is a 19.3 x 12.7 x 8.0 mm body whose male plug leaves the
//  wide face 4.8 mm from one end and whose female socket sits in the far
//  end face; plugged in, the body hangs below its port with its length
//  running front to back and the socket facing the back wall, and the
//  two bodies touch (ports 12.74 mm apart). The back wall gets one window
//  over both socket faces (they end 2.9 mm inside the outer face) with a
//  1 mm chamfer, a U-shaped cradle on the cavity back keys the last 6 mm
//  of the bodies, and one groove beside the right end of the window marks
//  USB. The bodies' near ends reach 1.1 mm in front of the frame's back
//  face, so the frame's plate strip below the ports has to be notched by
//  hand (see README). v4's sockets, ribbon hooks and vent keep-outs are
//  gone; the lower vent band regains its +-30 columns. Based on v4 (front
//  bar kept, not the v3 plinth; no v5 speaker).
//
//  v6 edges (2026-09-19, edited in place, still v6): the outer edges are
//  softened for the hand. The back face's perimeter gets a 45-degree
//  chamfer (edge_back_c: it is the bed side of the print, where a fillet
//  would overhang), the front rim's outer edge a fillet (edge_rim_r,
//  leaving a flat next to the pocket, whose own edge stays sharp), and the
//  base's front edge, the acute one under the LED face, a larger fillet
//  (edge_base_r) that blends into the rim's along the front corners. The
//  six counterbore mouths get a small chamfer (cb_chamfer). The pocket,
//  ledge, bosses, cradle and every other opening are unchanged. The outer
//  body is now the hull of thin sections of the same envelope (side, top,
//  base, front and back planes), each section inset by its own edge
//  treatment, so the envelope itself did not move: set the three edge
//  values to 0 and the old body comes back exactly.
//
//  v7 (2026-09-22): v6 made tolerant of the inputs it cannot verify. The
//  port position is read off a product photo (+-1 mm), the adapter was
//  measured on a photo, the boot gap that sets how far the bodies hang
//  is an estimate, and an FDM opening prints smaller than modelled; v6's
//  window (0.4/0.6 mm per side) and cradle (0.3/0.5) could not absorb
//  that. v7 names the budget (tol_*, print_clr) and derives every
//  clearance from it: the window is sized for the worst case (its outer
//  chamfer shrinks to 0.5 mm and the mic1 hole moves 1.7 mm away from it
//  so the webs stay at 2 mm; downwards the frame's own wall bounds the
//  error, so that side stays close), and the seating ledge is relieved
//  below the ports. The cradle is no longer part of the shell: it is a
//  separate small print (part = "insert"), a U with v6's tight clearances
//  on a 1 mm sole, glued to the cavity back around the window while it
//  sits on the real adapters, so it aligns itself to wherever they are.
//  Two rails on the cavity back bracket the sole and key the glue joint.
//  Everything else is v6, edges included.
//
//  v7, 2026-09-22 (later, both variants): the walls wrap 1.5 mm further
//  forward, over the edge of the LED board, so the mask stands 1.0 mm
//  proud of the rim instead of 2.5 (`proud`; the lip is derived). The
//  board's outline is not in any drawing and is assumed 128.0 mm
//  (`board`); a stepped pocket appears automatically if board + clearance
//  exceeds the frame pocket. The shell is 1.5 mm deeper at the front,
//  nothing inside moves.
// ---------------------------------------------------------------------
//  Coordinates (design orientation, part = "shell"):
//    origin = centre of the panel frame back face
//    +X = right when looking at the BACK of the panel
//    +Y = up (the desk is towards -Y)
//    +Z = away from the LEDs, into the shell
//  The panel is installed rotated by panel_rot (90 deg CCW seen from
//  the back) so the controller USB-C ports face DOWN. The LED image
//  therefore has to be rotated 90 deg in firmware.
//
//  The shell is a wedge: depth_bottom deep where the controller and
//  cables live, thinning to depth_top at the top edge. The back face is
//  one flat inclined plane, so the print still lies flat on it.
//
//  part = "print"       -> back face on the bed, no supports needed
//  part = "assembly"    -> shell + translucent panel / chip / adapter / encoder mock-ups
//  part = "section_ad"  -> cut through the left (POWER) adapter
//  part = "section_win" -> cut along the adapters' centre line, seen from below
//  part = "section_y"   -> cut through the side screw bosses
//  part = "section_enc" -> cut through the right-hand encoder
//  part = "section_edge"-> cut through the walls at x = section_edge_x, clear of every feature: the edge profiles
//  part = "product"     -> the assembled display standing on a table (LED face mock-up)
//  part = "frame"       -> panel frame and controller only, seen from the back: where to notch the frame plate
//  part = "insert"      -> the cradle insert alone, sole on the bed (v7; a second, separate print)
// =====================================================================

part = "shell";            // "shell" | "print" | "assembly" | "section_ad" | "section_win" | "section_y" | "section_enc" | "section_edge" | "product" | "frame" | "insert"
section_edge_x = 33;       // where "section_edge" cuts: between two vent columns, clear of the encoder boards and the bosses

/* [Panel] */
panel      = 127.8;        // frame outer size, measured in RGB-Matrix-P2-64x64-2D.dwg (Waveshare quotes 128)
panel_clr  = 0.3;          // clearance per side between panel and pocket
frame_d    = 12;           // depth of the plastic frame behind the LED PCB
panel_stack = 2.5;         // LED board + mask in front of the frame's front face (measured on the v1 print: the mask
                           // stood 2.5 mm proud of a rim that ended at the frame)
proud      = 1.0;          // how far the mask stands proud of the rim (user choice 2026-09-22; was 2.5 = frame only)
lip        = frame_d + panel_stack - proud;   // how far the walls wrap forward over the frame: 13.5
board      = 128.0;        // LED board / mask outline (assumed: Waveshare quotes 128; not in any drawing, measure it)
board_clr  = 0.2;          // clearance per side beside the board; the pocket steps out if board + 2*board_clr exceeds it
panel_rot  = 90;           // panel rotation inside the shell, CCW seen from the back
// M3 inserts in the panel's own orientation (arrows up), from RGB-Matrix-P2-64x64-2D.dwg
holes_native    = [[0,56.85],[0,-56.85],[56.85,44],[-56.85,44],[56.85,-44],[-56.85,-44]];
hub75_in_native = [-35.0, 5.4];   // centre of the HUB75 IN header, panel orientation, seen from the back
frame_wall_t    = 1.6;            // the frame's outer wall (drawing); its inner face is at panel/2 - 1.6
frame_open      = 58.4;           // half-width of the frame's back opening at the ports (|y| < 16 native), from the drawing

/* [Shell] */
wall         = 2.0;    // side wall thickness
back_t       = 2.4;    // back wall thickness (perpendicular to the back face)
depth_bottom = 22;     // outer depth behind the frame back face at the bottom edge
depth_top    = 8;      // outer depth at the top edge (>= 8 keeps the top screw heads recessed)
tilt         = 12;     // lean-back angle in degrees
r_in         = 0.6;    // pocket corner radius (the frame corners are sharp; keep small)
ledge_w      = 1.6;    // seating ledge at the frame back face (0 = none); the frame's outer wall is 1.6 mm
ledge_t      = 1.5;

/* [Edges] */
edge_back_c = 1.4;     // 45-degree chamfer on the back face's perimeter (the bed side of the print, where a fillet would
                       // overhang). 1.4 keeps 2.0 mm of material on the top edge's corner diagonal (the top wall meets
                       // the back at 96 degrees, so it is the thinnest corner); 1.5 would leave 1.95. See the echo.
edge_rim_r  = 1.5;     // fillet on the front rim's outer edge; the rim is `wall` wide, so wall - edge_rim_r stays flat
                       // next to the pocket and the pocket edge itself stays sharp
edge_base_r = 3.0;     // fillet on the base's front edge (the 78-degree edge under the LED face, solid behind it);
                       // >= edge_rim_r, it blends into the rim fillet along the two front-bottom corners
edge_fn     = 12;      // facets per quarter turn of the rim fillet (7.5 degrees each, like $fn = 48)
cb_chamfer  = 0.5;     // 45-degree chamfer on the six counterbore mouths (0 = sharp)

/* [Screws] */
screw_len  = 10;   // M3 screw length
engage     = 5;    // thread engagement inside the brass insert
screw_hole = 3.4;  // clearance hole
cb_d       = 6.5;  // counterbore for head and driver
boss_od    = 10.5; // leaves 2.0 mm walls around the counterbore (JLC3DP preference)
web_t      = 2;    // rib joining each boss to the nearest wall

/* [Controller] */
z_chip      = 0.5;              // chip PCB back face above the frame back face; the chip edge overhangs the
                                // frame rim by ~0.8 mm, so it must rest at or just above the rim (0..1 mm).
                                // 2026-09-19: an adapter resting on the frame plate stops ~1 mm short of
                                // seating, which puts the receptacle axis ~3.8 mm behind the frame face: consistent
chip_size   = [50.01, 42];
chip_socket = [17.69, 10.54];   // HUB75 socket centre from the chip's top-left corner
chip_t      = 1.6;
chip_comp_h = 5.6;
usb_yc      = [9.86, 22.6];     // USB-C port centres measured from the chip's top edge (POWER, USB)
usb_w       = 8.94;
usb_h       = 3.2;              // receptacle height on the PCB's back side; its axis is usb_h/2 above the PCB
boot_c      = [22.34, 3.24];    // button centres from the chip's top-left corner
rst_c       = [27.39, 3.24];
mic1_c      = [2.2, 30.7];      // microphones (from the product photo, +-1 mm)
mic2_c      = [47.8, 30.7];

/* [USB-C adapters] */
// Small 90-degree USB-C adapter (male plug on the wide face, female socket in the far end face), hand-measured
// 2026-09-19, see input/measurements.md and input/usb-c-90-degree-adapter/. One per port, POWER left, USB right.
adapters    = true;
ad_len      = 19.3;    // body length, near end face to female socket face
ad_w        = 12.7;    // body width across the end face; the two bodies touch at the ports' 12.74 mm pitch
ad_t        = 8.0;     // body thickness along the plug axis
ad_r        = 3.0;     // corner radius of the body cross-section (looks like ~3.5 to 4; 3.0 keeps sharper corners clear)
ad_plug_c   = 4.8;     // near end face to the male plug's centre (19.3 - 13.3 - 1.2)
ad_plug_l   = 7.7;     // plug beyond the body face incl. the boot (15.7 - 8.0); 6.5 of it is the shell
ad_gap      = 1.2;     // body face to the receptacle face when mated (the boot); estimated
ad_chamfer  = 1;       // 45-degree entry chamfer on the insert's opening (the bodies slide in with the panel)
ad_win_chamfer = 0.5;  // 45-degree chamfer on the window's outer edge (v6: 1.0; smaller so the web to the mic1 hole keeps 2 mm)
ad_mark     = true;    // one groove beside the right end of the window = USB (programming); POWER has none
cable_boot_l = 14;     // mock-up only: a straight cable plug in the POWER adapter (boot length, cable stub)
cable_stub   = 18;

/* [Tolerance budget (v7)] */
// What the model cannot know exactly and how far it may be off. Every clearance around the adapters is derived from
// these, so the print fits at the first try; once the real positions are measured (README, v7) the numbers can shrink.
tol_pos    = [1.0, 1.0];  // port position in x and y: hub75_in_native is read off a product photo, +-1 mm (README)
tol_gap    = 0.5;         // ad_gap, the boot between body and receptacle: an estimate; moves the bodies in y
tol_body   = 0.2;         // the adapter's body size, measured on a photo, per side of the pair
tol_z      = 1.1;         // depth of the socket faces: z_chip (0..1 mm) + ad_plug_c + ad_len; the well absorbs it, reported only
print_clr  = 0.25;        // an FDM opening prints this much smaller per side (a guess from the v1 print: +0.3 mm per side outside)
win_slack  = 0.1;         // extra on the window's lower edge beyond the physical bound (see ad_win_dn)

/* [Cradle insert (v7)] */
ins_clr     = [0.3, 0.5];  // opening clearance per side in x and y, before print_clr (v6's cradle values); the insert is glued
                           // in place on the real adapters, so it takes no share of the position budget
ins_depth   = 6;           // sole + walls, in front of the cavity back (v6's cradle depth)
ins_sole_t  = 1.0;         // the plate that glues to the cavity back around the window (a glued lamination, hence under 2 mm)
ins_wall_t  = 2;           // side walls
ins_bot_t   = 1.0;         // bottom wall: the room between the bodies and the shell's bottom wall, which backs it, is 1.6 mm at
                           // most and 1.15 when the bodies sit as low as they physically can (see the echo)
ins_lap     = 3;           // how far the sole reaches beyond the window on each side (the glue land)
ins_top_cut = 2.5;         // the sole ends this far below the bodies' top face: clear of the mic1 hole even at the worst case
ins_rails   = true;        // two ribs on the cavity back bracketing the sole: a guide for placing it, a shear key for the glue
ins_rail_h  = 1.2;
ins_rail_t  = 1.5;
ledge_relief = true;       // the seating ledge is cut away below the ports (v6 passed the bodies 0.7 mm above it)
mic1_off    = [0.5, 1.6];  // the mic1 hole moves this far from its v6 place, away from the wider window; the microphone sits
                           // about 10 mm behind the wall, so the hole is a sound vent and its exact place does not matter

/* [Back wall features] */
vents           = true;
vent_w          = 1.6;
vent_pitch      = 6;
vent_xmax       = 42;
vent_bands      = [[28, 40], [-28, 36]];   // [centre y, slot length]; lower band shorter to clear the mic holes
vent_skip_lower = [-18, -12];   // columns left out of the lower band for BOOT/RST
pin_d      = 3.0;   // pin holes over BOOT / RESET (+-0.75 mm header-position slack, 2 mm bridge between them)
mic_d      = 3.5;   // microphone holes
// groove marks beside the pin holes instead of text: one groove = BOOT, two grooves = RESET
mark_w     = 1.2;   // groove width  (>= 0.8 mm feature, >= 1.2 mm ribs between grooves)
mark_l     = 3.0;   // groove length
mark_d     = 0.8;   // groove depth
mark_gap   = 1.5;   // material left between two grooves

/* [Encoders] */
encoders    = true;
enc_pos     = [[47, -25], [-47, -25]];  // shaft exit points on the outer back face: 19 mm in from each side edge,
                                        // 41 mm up from the bottom edge; clear of the (+-56.85, 0) bosses,
                                        // the (+-44, -56.85) bosses and the controller
enc_rot     = [-90, 90];                // board rotation about its shaft, per encoder: +-90 puts the two STEMMA QT
                                        // sockets on the top/bottom edges (the side wall is only 4.5 mm away)
                                        // and the header pads on the inner edge
// Adafruit 5880 (same PCB as 4991), from the EagleCAD board file
enc_board   = 25.4;     // square PCB, corner radius 2.54
enc_board_r = 2.54;
enc_board_t = 1.6;
enc_hole_p  = 20.32;    // four 2.5 mm plated mounting holes on this square, encoder at the centre
enc_hole_d  = 2.5;
// Bourns PEC11 15 mm-shaft encoder as soldered on the 5880 (PEC11 datasheet)
enc_body_h  = 6.5;      // PCB top face to the bushing base (the panel mounting surface)
enc_body_w  = 13.2;     // body footprint 12.5 x 13.2, sits at 45 deg on the board
enc_bush_d  = 7.0;      // M7 x 0.75 bushing
enc_bush_l  = 5.0;      // threaded length (7.0 on the 20 mm-shaft PEC11R-4220F)
enc_shaft_l = 15;       // bushing base to shaft tip
enc_shaft_d = 6.0;
enc_nut_h   = 2.5;      // flat washer + hex nut, as supplied with the encoder
// shell features
enc_wall_hole = 7.4;    // clearance hole for the M7 bushing
enc_spot_d    = 14;     // spot-face on the outer face: a flat seat for washer and nut on the sloping wall
enc_spot_t    = 0.4;    // its depth; the nut then gets enc_bush_l - (back_t - enc_spot_t) = 3.0 mm of thread
enc_post_d    = 4.5;    // locating posts under the four board holes (4.5 stays clear of the pads and jumpers)
enc_peg_d     = 2.2;    // peg entering the 2.5 mm plated hole
enc_peg_h     = 2.0;    // board thickness + 0.4 mm
enc_keepout   = 2.5;    // vent slots closer than this to a board are dropped
knob_d = 20;            // mock-up only: Adafruit 5527..5531 machined knob
knob_h = 12.5;

$fn = 48;

// ---------------- derived ----------------
half_in  = panel/2 + panel_clr;
half_out = half_in + wall;
board_half = max(half_in, board/2 + board_clr);                // pocket half-width beside the board (z in -lip..-frame_d)
board_wall = half_out - board_half;                            // wall left beside the board
r_out    = r_in + wall;
back_ang = atan((depth_bottom - depth_top) / (2*half_out));   // slope of the back face
z_mid    = (depth_bottom + depth_top) / 2;                     // back face height at y = 0
back_tz  = back_t / cos(back_ang);                             // back wall thickness measured along Z
function z_back(y) = z_mid - y * tan(back_ang);                // outer back face
function z_cav(y)  = z_back(y) - back_tz;                      // cavity back face
wedge    = (depth_bottom + lip) * tan(tilt);                   // extra height of the wedge at the front bottom
floor_t  = screw_len - engage;                                 // boss floor between screw head and panel

// the two inclined planes of the outer envelope, as lines in the YZ side view (2D normals point outwards, n . p = k)
n_base = [-cos(tilt), sin(tilt)];      k_base = n_base * [-half_out, depth_bottom];   // the base, through the back-bottom edge
n_bk   = [sin(back_ang), cos(back_ang)]; k_bk  = n_bk * [0, z_mid];                    // the back face
function isect2(n1, k1, n2, k2) = let (det = n1[0]*n2[1] - n1[1]*n2[0])              // where two such lines cross
    [(k1*n2[1] - k2*n1[1]) / det, (n1[0]*k2 - n2[0]*k1) / det];
function y_base(z, d = 0) = (k_base - d - n_base[1]*z) / n_base[0];                   // the base plane, moved inwards by d, at height z
function dist_line(p, a, b) = abs((b[0]-a[0])*(p[1]-a[1]) - (b[1]-a[1])*(p[0]-a[0])) / norm(b - a);
// corner material left by the back chamfer: distance from the cavity's edge to the chamfer plane, per edge
edge_top_pts = [[half_out - edge_back_c, z_back(half_out - edge_back_c)], [half_out, z_back(half_out) - edge_back_c / cos(back_ang)]];
edge_bot_pts = [isect2(n_base, k_base - edge_back_c, n_bk, k_bk), isect2(n_base, k_base, n_bk, k_bk - edge_back_c)];
corner_top  = dist_line([half_in, z_cav(half_in)], edge_top_pts[0], edge_top_pts[1]);
corner_bot  = dist_line([-half_in, z_cav(-half_in)], edge_bot_pts[0], edge_bot_pts[1]);
corner_side = (wall + back_t - edge_back_c) / sqrt(2);   // the side walls meet the back face at 90 degrees

function rot2(p, a) = [p[0]*cos(a) - p[1]*sin(a), p[0]*sin(a) + p[1]*cos(a)];
function contains(v, x) = len([for (e = v) if (abs(e - x) < 1e-6) e]) > 0;

holes           = [for (h = holes_native) rot2(h, panel_rot)];
chip_org_native = [hub75_in_native[0] - chip_socket[0], hub75_in_native[1] + chip_socket[1]];
function chip_pt(xc, yc) = rot2([chip_org_native[0] + xc, chip_org_native[1] - yc], panel_rot);

boot = chip_pt(boot_c[0], boot_c[1]);
rst  = chip_pt(rst_c[0],  rst_c[1]);
mic1 = chip_pt(mic1_c[0], mic1_c[1]) + mic1_off;   // v7: moved away from the window
mic2 = chip_pt(mic2_c[0], mic2_c[1]);
usb  = [for (y = usb_yc) chip_pt(0, y)];      // [POWER, USB] port centres on the chip's bottom edge
chip_top_y = max([for (c = [[0,0],[chip_size[0],0],chip_size,[0,chip_size[1]]]) chip_pt(c[0], c[1])[1]]);

// adapters: the pair hangs below the ports, bodies along Z, female faces towards the back wall
port_z    = z_chip + chip_t + usb_h/2;                                  // receptacle axis behind the frame back face
ad_ctr    = [(usb[0][0] + usb[1][0])/2, usb[0][1] - ad_gap - ad_t/2];   // centre of the pair's cross-section (x, y)
ad_pair_w = abs(usb[1][0] - usb[0][0]) + ad_w;                          // both bodies side by side
ad_z0     = port_z - ad_plug_c;                                         // near end faces (negative = in front of the frame face)
ad_z1     = ad_z0 + ad_len;                                             // female socket faces
ad_zc     = z_cav(ad_ctr[1]);                                           // cavity back at the pair centre
ad_y_bot  = ad_ctr[1] - ad_t/2;                                         // bodies' lower face
frame_wall_y = -(panel/2 - frame_wall_t);                               // inner face of the frame's outer wall
ledge_y      = -half_in + ledge_w;                                      // inner edge of the shell's seating ledge
// v7: the window's clearances follow from the budget. Downwards the bodies cannot be lower than the frame's own wall
// (they would not seat), so that side needs only the print allowance beyond the physical bound.
ad_phys_dn = ad_y_bot - frame_wall_y;                                   // how much lower the bodies can physically be
ad_win_x   = tol_pos[0] + tol_body + print_clr;                         // window clearance per side in x
ad_win_up  = tol_pos[1] + tol_gap + tol_body + print_clr;               // above the bodies
ad_win_dn  = ad_phys_dn + print_clr + win_slack;                        // below the bodies
ad_win     = [ad_pair_w + 2*ad_win_x, ad_t + ad_win_up + ad_win_dn, ad_r];   // wall window: w, h, corner r (= the body's, so a body
                                                                        // pushed into a corner by the whole budget still clears it)
ad_win_cy  = ad_ctr[1] + (ad_win_up - ad_win_dn)/2;                     // window centre at the INNER face (global y)
ad_win_y   = ad_win_cy + back_t * sin(back_ang);                        // on_back origin that centres the window there
// v7: the insert (its opening is centred on the real pair, so only the print allowance is added to v6's clearances)
ins_in     = [ad_pair_w + 2*(ins_clr[0] + print_clr), ad_t + 2*(ins_clr[1] + print_clr), ad_r + ins_clr[1] + print_clr];   // opening: w, h, corner r
ins_ow     = ins_in[0] + 2*ins_wall_t;                                  // outer width across the side walls
ins_y_top  = ad_ctr[1] + ad_t/2;                                        // side walls end at the bodies' top face (as v6)
ins_y_bot  = ad_ctr[1] - ins_in[1]/2 - ins_bot_t;                       // underside of the bottom wall
ins_sole   = [ad_win[0] + 2*ins_lap, ins_y_top - ins_top_cut - ins_y_bot];   // sole: w, h (open at the top like the U)
ins_sole_cy = (ins_y_top - ins_top_cut + ins_y_bot)/2;                  // sole centre (global y)
ins_rail_x = ins_sole[0]/2 + ad_win_x + 0.3;                            // rail inner faces from the pair centre: the sole may sit anywhere within the x budget
ledge_relief_w = ad_pair_w + 2*(ad_win_x + 1);                          // the ledge is cut away over this width below the ports
// webs around the mic1 hole: to the window's chamfer outline on the outer face (corner arcs of radius ad_win[2] + ad_win_chamfer),
// to the window itself at the inner face, and to the nearest lower-band vent slot end
ad_corner_c  = [ad_ctr[0] + ad_win[0]/2 - ad_win[2], ad_win_y + ad_win[1]/2 - ad_win[2]];
ad_mic_gap   = norm(mic1 - ad_corner_c) - (ad_win[2] + ad_win_chamfer) - mic_d/2;
mic1_vent_gap = min([for (x = [-vent_xmax : vent_pitch : vent_xmax]) if (!contains(vent_skip_lower, x))
    norm(mic1 - [x, vent_bands[1][0] - (vent_bands[1][1] - vent_w)/2]) - vent_w/2 - mic_d/2]);

// encoder stack, measured along the back-face normal from the OUTER face (negative = inwards)
enc_z_board  = -back_t - enc_body_h;                 // board top face (component side, towards the wall)
enc_z_bottom = enc_z_board - enc_board_t;            // board bottom face
enc_z_peg    = enc_z_board - enc_peg_h;              // peg tips
enc_post_xy  = [for (sx = [-1, 1], sy = [-1, 1]) [sx*enc_hole_p/2, sy*enc_hole_p/2]];
function enc_global_z(p, zloc) = z_back(p[1]) + zloc * cos(back_ang);   // global Z of a point zloc below the face at p
function enc_post_global(p, q) = [p[0] + q[0], p[1] + q[1] * cos(back_ang) + enc_z_board * sin(back_ang)];
function enc_boss_gap(p) = min([for (q = enc_post_xy, h = holes)
    norm(enc_post_global(p, q) - h) - enc_post_d/2 - boss_od/2]);
// a vent slot (column x, band centre y0, length l) is dropped when it would run into an encoder board
function rect_blocks(x, y0, l, c, w, h, ko) = abs(x - c[0]) < w/2 + ko + vent_w/2 && abs(y0 - c[1]) < h/2 + ko + l/2;
function enc_blocks(x, y0, l) =
    encoders && len([for (p = enc_pos) if (rect_blocks(x, y0, l, p, enc_board, enc_board, enc_keepout)) 1]) > 0;

echo(str("outer size X x Y (front) = ", 2*half_out, " x ", 2*half_out + wedge,
         "  depth bottom/top = ", depth_bottom + lip, "/", depth_top + lip, "  back slope = ", back_ang, " deg"));
echo(str("front: lip ", lip, " = frame ", frame_d, " + stack ", panel_stack, " - proud ", proud, "  board ", board, " + ", board_clr,
         " per side -> pocket beside the board ", 2*board_half, " (frame pocket ", 2*half_in, ")  wall there ", board_wall, " (rule: >= 2)"));
if (board_wall < 2) echo("WARNING: the wall beside the LED board is under 2 mm");
echo(str("clear depth: bottom edge ", z_cav(-half_in), "  chip top edge (y=", chip_top_y, ") ", z_cav(chip_top_y),
         "  y=0 ", z_cav(0), "  top edge ", z_cav(half_in), "  wedge = ", wedge));
echo(str("holes = ", holes, "  top boss back face z = ", z_back(max([for (h = holes) h[1]]))));
echo(str("chip corners = ", chip_pt(0,0), " ", chip_pt(chip_size[0],0), " ", chip_pt(chip_size[0],chip_size[1]), " ", chip_pt(0,chip_size[1])));
echo(str("usb ports = ", usb, "  boot = ", boot, "  rst = ", rst, "  mic1 = ", mic1, "  mic2 = ", mic2));
if (adapters) {
    echo(str("adapters: port axis z ", port_z, "  near ends z ", ad_z0, " (frame back face 0: notch the frame plate at least ", -ad_z0 + 0.5,
             " deep)  female faces z ", ad_z1, "  cavity back there ", ad_zc, "  outer face ", z_back(ad_ctr[1]),
             "  well depth ", z_back(ad_ctr[1]) - ad_z1));
    echo(str("adapters: bodies x ", usb[0][0] - ad_w/2, "..", usb[1][0] + ad_w/2, "  y ", ad_y_bot, "..", ad_ctr[1] + ad_t/2,
             "  lower face to the frame wall ", ad_y_bot - frame_wall_y, "  to the ledge ", ad_y_bot - ledge_y,
             "  over the frame plate ", -frame_open - ad_y_bot));
    echo(str("budget: window clearance x ", ad_win_x, " per side (= pos ", tol_pos[0], " + body ", tol_body, " + print ", print_clr,
             ")  up ", ad_win_up, " (= pos ", tol_pos[1], " + gap ", tol_gap, " + body ", tol_body, " + print ", print_clr,
             ")  down ", ad_win_dn, " (= physical ", ad_phys_dn, " + print ", print_clr, " + slack ", win_slack, ")"));
    echo(str("budget: socket faces z ", ad_z1, " +-", tol_z, " -> well depth ", z_back(ad_ctr[1]) - ad_z1 - tol_z, "..", z_back(ad_ctr[1]) - ad_z1 + tol_z,
             " (plug shell 6.5 mm, so the plugs never leave the receptacles)  at the shallow extreme the bodies end at z ", ad_z1 - tol_z,
             ", short of the sole (", ad_zc - ins_sole_t, ") but still ", ad_z1 - tol_z - (ad_zc - ins_depth), " mm inside the walls"));
    echo(str("adapters: window ", ad_win[0], " x ", ad_win[1], " (corner r ", ad_win[2], ", +", ad_win_chamfer, " chamfer)  centre y ", ad_win_cy,
             "  bottom edge y ", ad_win_cy - ad_win[1]/2, " (frame wall ", frame_wall_y, ", ledge ", ledge_y, ")"));
    echo(str("insert: opening ", ins_in[0], " x ", ins_in[1], "  outer width ", ins_ow, "  y ", ins_y_bot, "..", ins_y_top,
             "  sole ", ins_sole[0], " x ", ins_sole[1], " centred y ", ins_sole_cy, "  z ", ad_zc - ins_depth, "..", ad_zc,
             "  sole to the shell's bottom wall ", ins_y_bot + half_in, " (", ins_y_bot + half_in - ad_phys_dn, " at the physical bound)",
             "  rails at x ", ad_ctr[0] - ins_rail_x - ins_rail_t, "..", ad_ctr[0] - ins_rail_x, " and ", ad_ctr[0] + ins_rail_x, "..", ad_ctr[0] + ins_rail_x + ins_rail_t));
    echo(str("insert: at the worst case (bodies ", ad_win_up - print_clr, " higher) the side walls end at y ", ins_y_top + ad_win_up - print_clr,
             " and the sole at y ", ins_y_top - ins_top_cut + ad_win_up - print_clr, "  mic1 hole bottom edge y ", mic1[1] - mic_d/2, " (mic +-1)"));
    echo(str("webs: mic1 hole to the window at the inner face ", ad_mic_gap + ad_win_chamfer, " (rule: >= 2; on the outer face the ",
             ad_win_chamfer, " chamfer outline comes ", ad_mic_gap, " close)  to the nearest lower vent slot ", mic1_vent_gap, " (rule: >= 2)"));
    echo(str("ledge relief: x ", ad_ctr[0] - ledge_relief_w/2, "..", ad_ctr[0] + ledge_relief_w/2, " (", ledge_relief_w, " wide)"));
    echo(str("frame notch, panel native orientation (arrows up, seen from the back): x ", frame_wall_y, "..", -frame_open,
             "  y ", -(usb[1][0] + ad_w/2 + 1 + tol_pos[0]), "..", -(usb[0][0] - ad_w/2 - 1 - tol_pos[0]), " (", ad_pair_w + 2*(1 + tol_pos[0]),
             " wide, the whole budget; ", ad_pair_w + 2, " centred on the REAL ports is enough)  depth >= ", -ad_z0 + 0.5, " (through the plate)"));
    if (ad_mic_gap + ad_win_chamfer < 2 || mic1_vent_gap < 2) echo("WARNING: a web around the mic1 hole is under 2 mm");
}
if (encoders) for (p = enc_pos) {
    echo(str("encoder at ", p, ": cavity depth ", z_cav(p[1]), "  board bottom z ", enc_global_z(p, enc_z_bottom),
             "  peg tips z ", enc_global_z(p, enc_z_peg), " (ledge top ", ledge_t + ledge_w, ", frame back 0)",
             "  board edge to side wall ", half_in - abs(p[0]) - enc_board/2,
             "  post to nearest boss ", enc_boss_gap(p)));
    echo(str("encoder at ", p, ": bushing proud of face ", enc_bush_l - back_t, "  thread for washer+nut ",
             enc_bush_l - (back_t - enc_spot_t), " (needs ", enc_nut_h, ")  shaft proud of face ", enc_shaft_l - back_t,
             "  knob end behind frame back ", z_back(p[1]) + (enc_nut_h - enc_spot_t + knob_h) * cos(back_ang)));
}
echo(str("edges: back chamfer ", edge_back_c, " leaves on the corner diagonal top ", corner_top, "  bottom ", corner_bot,
         "  sides ", corner_side, " (rule: >= 2)  bed footprint ", 2*(half_out - edge_back_c), " x ",
         norm(edge_top_pts[0] - edge_bot_pts[0])));
echo(str("edges: rim fillet ", edge_rim_r, " leaves a ", wall - edge_rim_r, " flat next to the pocket  base front fillet ", edge_base_r,
         " moves the base's front contact line back ", edge_base_r / tan((90 - tilt) / 2),
         "  window chamfer to the bottom edge chamfer on the back face ",
         adapters ? (ad_win_y - (ad_win[1]/2 + ad_win_chamfer) * cos(back_ang) - edge_bot_pts[0][0]) / cos(back_ang) : 0));
if (corner_top < 2 || corner_bot < 2 || corner_side < 2) echo("WARNING: the back chamfer leaves less than 2 mm at a corner");

// ---------------- primitives ----------------
module rrect(w, h, r) { offset(r = r) square([w - 2*r, h - 2*r], center = true); }

// local frame lying on the outer back face at height y: XY in the face, +Z outwards
module on_back(y) { translate([0, y, z_back(y)]) rotate([-back_ang, 0, 0]) children(); }

// The outer envelope is bounded by the side planes x = +-half_out, the top y = half_out, the tilted base, the front
// z = -lip and the inclined back face. Its body is the hull of thin sections of that envelope taken in planes parallel
// to the front or to the back, each with every bounding plane moved inwards by d (corner radius r_out - d, so the
// vertical corners come out at r_out again): a section with d = 0 is the plain outline, a stack of sections with
// d = r (1 - sin a) at depth r (1 - cos a) sweeps a fillet of radius r along the edge, and two sections d = c at the
// face plus d = 0 at depth c make a 45-degree chamfer.
module front_section(z0, d, ycut = undef) {      // in the plane z = z0; ycut trims it above the base bar's region
    vb = is_undef(ycut) ? y_base(z0, d) : max(y_base(z0, d), ycut);
    vt = half_out - d;
    translate([0, 0, z0]) linear_extrude(0.01) translate([0, (vt + vb)/2]) rrect(2*(half_out - d), vt - vb, r_out - d);
}
module back_section(w, d) {                      // in the plane parallel to the back face, w behind it
    q  = isect2(n_base, k_base - d, n_bk, k_bk - w);                  // where the (inset) base meets that plane
    vb = (q[0] + w*sin(back_ang)) / cos(back_ang);                     // global y -> position along the face
    vt = (half_out - d + w*sin(back_ang)) / cos(back_ang);
    translate([0, 0, z_mid]) rotate([-back_ang, 0, 0]) translate([0, 0, -w - 0.01])
        linear_extrude(0.01) translate([0, (vt + vb)/2]) rrect(2*(half_out - d), vt - vb, r_out - d);
}
module base_bar() {                              // the base's front edge: a capsule of radius edge_base_r tangent to the front,
    r = edge_base_r; zc = -lip + r;              // the base and both side planes; its end spheres are the front-bottom corners
    hull() for (sx = [-1, 1]) translate([sx*(half_out - r), y_base(zc, r), zc]) sphere(r);
}

module outer_body() {
    bar = edge_base_r > edge_rim_r;              // the rim sections stop above the bar, else their smaller fillet would win the hull
    hull() {
        for (a = [0 : 90/edge_fn : 90])
            front_section(-lip + edge_rim_r*(1 - cos(a)), edge_rim_r*(1 - sin(a)), bar ? -half_out : undef);
        if (bar) base_bar();
        back_section(0, edge_back_c);
        back_section(edge_back_c, 0);
    }
}

module below_cavity_back() {           // half-space under the cavity back plane
    translate([0, 0, z_mid - back_tz]) rotate([-back_ang, 0, 0])
        translate([-200, -200, -400]) cube([400, 400, 400]);
}

module cavity() {
    intersection() {
        translate([0, 0, -lip - 1]) linear_extrude(lip + 1 + depth_bottom) rrect(2*half_in, 2*half_in, r_in);
        below_cavity_back();
    }
    if (board_half > half_in)           // stepped pocket beside the LED board (only when the board is wider than the frame pocket)
        translate([0, 0, -lip - 1]) linear_extrude(lip + 1 - frame_d) rrect(2*board_half, 2*board_half, r_in);
}

module ledge() {
    if (ledge_w > 0) difference() {
        linear_extrude(ledge_t + ledge_w) rrect(2*half_in + 0.2, 2*half_in + 0.2, r_in);
        hull() {
            translate([0, 0, -1])
                linear_extrude(1 + ledge_t) rrect(2*(half_in - ledge_w), 2*(half_in - ledge_w), r_in + ledge_w);
            translate([0, 0, ledge_t + ledge_w + 0.5])
                linear_extrude(0.01) rrect(2*half_in + 1, 2*half_in + 1, r_in + 0.5);
        }
    }
}

module boss(p) {                        // tall; clipped to the outer body in shell()
    h = depth_bottom + 1;
    translate([p[0], p[1], 0]) cylinder(d = boss_od, h = h);
    if (abs(p[0]) > abs(p[1])) {        // boss near a side wall: rib along X
        sx  = p[0] > 0 ? 1 : -1;
        len = half_in + 1 - abs(p[0]);
        translate([(p[0] + sx*(half_in + 1))/2, p[1], h/2]) cube([len, web_t, h], center = true);
    } else {                             // boss near top/bottom wall: rib along Y
        sy  = p[1] > 0 ? 1 : -1;
        len = half_in + 1 - abs(p[1]);
        translate([p[0], (p[1] + sy*(half_in + 1))/2, h/2]) cube([web_t, len, h], center = true);
    }
}

module boss_hole(p) {
    translate([p[0], p[1], -1])       cylinder(d = screw_hole, h = depth_bottom + 3);
    translate([p[0], p[1], floor_t])  cylinder(d = cb_d, h = depth_bottom + 3);
    if (cb_chamfer > 0) on_back(p[1]) translate([p[0], 0, -cb_chamfer])            // chamfered mouth on the outer face
        cylinder(d1 = cb_d, d2 = cb_d + 2*(cb_chamfer + 1), h = cb_chamfer + 1);
}

// ---------------- USB-C adapters: window, mark, ledge relief, rails ----------------
module ad_win2d()  { rrect(ad_win[0], ad_win[1], ad_win[2]); }

module ad_window() {                    // through the back wall over both socket faces, chamfered outside
    on_back(ad_win_y) translate([ad_ctr[0], 0, 0]) {
        translate([0, 0, -back_tz - 0.01]) linear_extrude(back_tz + 2) ad_win2d();
        hull() {
            translate([0, 0, -ad_win_chamfer]) linear_extrude(0.01) ad_win2d();
            translate([0, 0, 1])               linear_extrude(0.01) offset(r = ad_win_chamfer + 1) ad_win2d();
        }
    }
}

module ad_groove() {                    // one groove beside the right end of the window: marks the USB (programming) socket
    on_back(ad_win_y) translate([ad_ctr[0] + ad_win[0]/2 + ad_win_chamfer + mark_gap + mark_w/2, 0, -mark_d])
        linear_extrude(mark_d + 1) square([mark_w, mark_l], center = true);
}

module ledge_relief_cut() {             // the seating ledge goes away below the ports; the frame rests on it everywhere else
    translate([ad_ctr[0] - ledge_relief_w/2, -half_in, -1]) cube([ledge_relief_w, half_in + ledge_y + 0.2, ledge_t + ledge_w + 2]);
}

// local frame lying on the CAVITY back at height y: XY in the face, +Z outwards (into the wall)
module on_cav(y) { translate([0, y, z_cav(y)]) rotate([-back_ang, 0, 0]) children(); }

module ins_rails() {                    // two ribs bracketing the sole, 0.2 into the wall so they fuse with it
    on_cav(ins_sole_cy) for (s = [-1, 1])
        translate([ad_ctr[0] + s*(ins_rail_x + ins_rail_t/2), 0, -ins_rail_h])
            linear_extrude(ins_rail_h + 0.2) square([ins_rail_t, ins_sole[1]], center = true);
}

// ---------------- the cradle insert (v7): a separate print ----------------
// A U (bottom + two side walls, open towards the controller) around the last ins_depth mm of the bodies, standing on a
// 1 mm sole that lies on the cavity back and reaches ins_lap beyond the window on each side and below it. The bodies pass
// through the sole's opening into the wall's window. Glued to the cavity back while sitting on the real adapters, so its
// opening keeps v6's tight clearances whatever the real position is; the shell's window and rails leave it the whole budget.
module ins_open2d() { rrect(ins_in[0], ins_in[1], ins_in[2]); translate([-ins_in[0]/2, 0]) square([ins_in[0], 30]); }   // open towards the controller

module insert_in_place() {              // where it sits in the shell (design coordinates)
    z0 = ad_zc - ins_depth;
    difference() {
        intersection() {
            union() {
                on_cav(ins_sole_cy) translate([ad_ctr[0], 0, -ins_sole_t]) linear_extrude(ins_sole_t + 0.01) rrect(ins_sole[0], ins_sole[1], 1);
                translate([ad_ctr[0] - ins_ow/2, ins_y_bot, z0]) cube([ins_ow, ins_y_top - ins_y_bot, ins_depth + 1]);   // the U block, along Z
            }
            below_cavity_back();        // both end at the cavity back
        }
        translate([ad_ctr[0], ad_ctr[1], z0 - 1]) linear_extrude(ins_depth + 3) ins_open2d();   // the bodies' prism, along Z
        hull() {                        // entry chamfer for the bodies sliding in with the panel
            translate([ad_ctr[0], ad_ctr[1], z0 - 0.01])       linear_extrude(0.01) offset(r = ad_chamfer) ins_open2d();
            translate([ad_ctr[0], ad_ctr[1], z0 + ad_chamfer]) linear_extrude(0.01) ins_open2d();
        }
    }
}

module insert_print() {                 // the insert alone, sole face down on the bed (the walls lean back_ang from vertical)
    mirror([0, 0, 1]) rotate([back_ang, 0, 0]) translate([-ad_ctr[0], -ins_sole_cy, -z_cav(ins_sole_cy)]) insert_in_place();
}

module vent_slot(x, y, l) {
    on_back(y) translate([x, 0, -back_tz - 1])
        linear_extrude(back_tz + 2) offset(r = vent_w/2) square([0.01, l - vent_w], center = true);
}

module vent_grid() {
    for (b = vent_bands) for (x = [-vent_xmax : vent_pitch : vent_xmax])
        if (!(b[0] < 0 && contains(vent_skip_lower, x)) && !enc_blocks(x, b[0], b[1])) vent_slot(x, b[0], b[1]);
}

module back_hole(p, d) {
    on_back(p[1]) translate([p[0], 0, -back_tz - 1]) cylinder(d = d, h = back_tz + 2);
}

module marks(p, n) {                    // n short grooves to the left of pin hole p
    for (i = [0 : n - 1])
        on_back(p[1]) translate([p[0] - (pin_d/2 + mark_gap + mark_w/2) - i*(mark_w + mark_gap), 0, -mark_d])
            linear_extrude(mark_d + 1) square([mark_w, mark_l], center = true);
}

// ---------------- encoders ----------------
// Four posts hang from the cavity back down to the board's component face; each ends in a peg that
// enters one of the board's 2.5 mm holes. The bushing base then touches the inner wall and the
// encoder's washer + nut clamp it from outside (in the spot-face), so the posts only key the board.
module enc_posts(p) {
    on_back(p[1]) translate([p[0], 0, 0]) for (q = enc_post_xy) translate([q[0], q[1], 0]) {
        translate([0, 0, enc_z_board]) cylinder(d = enc_post_d, h = enc_body_h + 0.5);            // post, 0.5 into the wall
        translate([0, 0, enc_z_peg + 0.4]) cylinder(d = enc_peg_d, h = enc_peg_h - 0.4 + 0.01);    // peg
        translate([0, 0, enc_z_peg]) cylinder(d1 = enc_peg_d - 0.8, d2 = enc_peg_d, h = 0.4 + 0.01); // chamfered tip
    }
}

module enc_holes(p) {
    on_back(p[1]) translate([p[0], 0, 0]) {
        translate([0, 0, -back_tz - 1]) cylinder(d = enc_wall_hole, h = back_tz + 2);   // bushing hole
        translate([0, 0, -enc_spot_t])  cylinder(d = enc_spot_d, h = enc_spot_t + 1);   // spot-face for washer + nut
    }
}

// ---------------- the part ----------------
module shell() {
    difference() {
        union() {
            difference() { outer_body(); cavity(); }
            difference() { ledge(); if (adapters && ledge_relief) ledge_relief_cut(); }
            intersection() { union() { for (p = holes) boss(p); } outer_body(); }
            if (encoders) for (p = enc_pos) enc_posts(p);
            if (adapters && ins_rails) ins_rails();
        }
        for (p = holes) boss_hole(p);
        if (adapters) { ad_window(); if (ad_mark) ad_groove(); }
        if (vents) vent_grid();
        back_hole(boot, pin_d);
        back_hole(rst,  pin_d);
        back_hole(mic1, mic_d);
        back_hole(mic2, mic_d);
        marks(boot, 1);
        marks(rst,  2);
        if (encoders) for (p = enc_pos) enc_holes(p);
    }
}

// ---------------- mock-ups for the assembly view ----------------
frame_plate_t = 2;      // mock-up only: thickness of the frame's back plate (a guess; the drawing gives only the 12 mm depth)
ghost_notch   = true;   // mock-up only: show the hand-cut notch in the frame plate below the ports

module frame_opening2d() {   // the frame's back opening in the panel's native orientation: +-51.9, widening to +-58.4 for |y| <= 16
    square(2*51.9, center = true);
    square([2*frame_open, 32], center = true);
    square([32, 2*frame_open], center = true);
}

module notch_block() {   // the material to remove from the frame's back plate: the strip between opening and wall, 1 mm wider than
    w = ad_pair_w + 2*(1 + tol_pos[0]);   // the pair plus the x budget (cut relative to the REAL ports, 1 mm each side is enough)
    translate([ad_ctr[0] - w/2, frame_wall_y - 0.01, -frame_plate_t - 0.01]) cube([w, -frame_wall_y - frame_open + 0.02, frame_plate_t + 1]);
}

module ghost_panel(notch = adapters && ghost_notch) {
    hin = rot2(hub75_in_native, panel_rot);
    color("dimgray", 0.35) difference() {
        translate([0, 0, -frame_d]) linear_extrude(frame_d) square(panel, center = true);
        translate([0, 0, -frame_d + 2]) linear_extrude(frame_d - 2 - frame_plate_t + 0.01)          // between front plate and back plate
            square(panel - 2*frame_wall_t, center = true);
        translate([0, 0, -frame_plate_t - 0.01]) linear_extrude(frame_plate_t + 1) rotate(panel_rot) frame_opening2d();   // the back plate's opening
        if (notch) notch_block();
    }
    color("darkgreen", 0.35) translate([0, 0, -frame_d - panel_stack]) linear_extrude(panel_stack - 1.0) square(board, center = true);   // LED board; the mask (1.0) is ghost_face()
    color("black", 0.6) translate([hin[0], hin[1], -frame_d]) linear_extrude(8.9) rotate(panel_rot) square([8.9, 20.3], center = true);
    pwr = rot2([12.3, 5.3], panel_rot);
    color("white", 0.6) translate([pwr[0], pwr[1], -frame_d]) linear_extrude(13) rotate(panel_rot) square([8, 16], center = true);
}

module chip_box(xc0, yc0, w, h, z0, hgt) {   // box in chip coordinates (x right, y down from top-left)
    p0 = chip_pt(xc0, yc0); p1 = chip_pt(xc0 + w, yc0); p2 = chip_pt(xc0 + w, yc0 + h); p3 = chip_pt(xc0, yc0 + h);
    translate([0, 0, z0]) linear_extrude(hgt) polygon([p0, p1, p2, p3]);
}

module ghost_chip() {
    color("royalblue", 0.6) chip_box(0, 0, chip_size[0], chip_size[1], z_chip, chip_t);
    color("silver", 0.7) for (y = usb_yc) chip_box(0, y - usb_w/2, 7.2, usb_w, z_chip + chip_t, usb_h);   // USB-C receptacles
    color("gray", 0.6)   chip_box(31.65, 7.14, 15.8, 17.1, z_chip + chip_t, 3.2);                          // ESP32 module
    color("white", 0.6)  chip_box(27, 36.5, 15, 5.5, z_chip + chip_t, chip_comp_h);                       // SPK + GPIO headers
    color("black", 0.6)  chip_box(chip_socket[0] - 4.45, chip_socket[1] - 10.15, 8.9, 20.3, z_chip - 8.5, 8.5);  // HUB75 socket
}

module ghost_adapters() {   // the two 90-degree adapters in the ports, socket faces towards the back wall
    for (i = [0 : 1]) { q = usb[i]; cy = ad_ctr[1];
        color([0.12, 0.12, 0.12], 0.9) difference() {                                                       // body with the socket cavity
            translate([q[0], cy, ad_z0]) linear_extrude(ad_len) rrect(ad_w, ad_t, ad_r);
            translate([q[0], cy, ad_z1 - 7]) linear_extrude(7.01) rrect(8.9, 3.2, 1.5);
        }
        color("goldenrod", 0.9) translate([q[0] - 4.15, cy + ad_t/2 - 0.01, port_z - 1.2]) cube([8.3, ad_gap + 6.5, 2.4]);   // male plug, 6.5 in the receptacle
    }
}

module ghost_cable() {   // a straight USB-C cable plug in the POWER adapter: 6.5 mm shell, 12 x 6.5 mm boot, cable stub
    q = usb[0]; cy = ad_ctr[1];
    color("silver", 0.9)   translate([q[0] - 4.15, cy - 1.2, ad_z1 - 6.5]) cube([8.3, 2.4, 7]);
    color([0.2, 0.2, 0.2]) translate([q[0], cy, ad_z1 + 0.5]) linear_extrude(cable_boot_l) rrect(12, 6.5, 2);
    color([0.2, 0.2, 0.2]) translate([q[0], cy, ad_z1 + 0.5 + cable_boot_l]) cylinder(d = 3.8, h = cable_stub);
}

module ghost_encoder(p, rot) {   // Adafruit 5880 board, encoder, washer + nut, knob
    on_back(p[1]) translate([p[0], 0, 0]) {
        color("darkgreen", 0.7) translate([0, 0, enc_z_bottom]) linear_extrude(enc_board_t)
            rotate(rot) rrect(enc_board, enc_board, enc_board_r);
        color("white", 0.7) for (s = [-1, 1]) rotate(rot)                                  // the two STEMMA QT sockets
            translate([s*(enc_board/2 - 2.1) - 3, -3, enc_z_board]) cube([6, 6, 2.9]);
        color("gray", 0.7) translate([0, 0, enc_z_board]) linear_extrude(enc_body_h)
            rotate(45) square([12.5, enc_body_w], center = true);
        color("silver", 0.8) translate([0, 0, -back_t]) cylinder(d = enc_bush_d, h = enc_bush_l);
        color("silver", 0.8) translate([0, 0, -back_t]) cylinder(d = enc_shaft_d, h = enc_shaft_l);
        color("dimgray", 0.9) translate([0, 0, -enc_spot_t]) cylinder(d = 10 / cos(30), h = enc_nut_h, $fn = 6);
        color([0.15, 0.15, 0.15]) translate([0, 0, -enc_spot_t + enc_nut_h + 0.3]) cylinder(d = knob_d, h = knob_h);   // opaque, dark enough to read as black but still shaded
    }
}

module ghost_insert() { color("tomato", 0.95) insert_in_place(); }   // the separate print, in place

module ghosts() { ghost_panel(); ghost_chip(); if (adapters) { ghost_adapters(); ghost_cable(); ghost_insert(); }
                  if (encoders) for (i = [0 : len(enc_pos) - 1]) ghost_encoder(enc_pos[i], enc_rot[i]); }

// ---------------- product view: the assembled display standing on a table ----------------
module ghost_face() {   // the LED mask: matt black front with a faint 2 mm pixel grid
    z0 = -frame_d - panel_stack;
    color("black") translate([0, 0, z0]) linear_extrude(1.0) square(board, center = true);
    color([0.2, 0.2, 0.2]) for (i = [1 : 63]) {
        translate([-panel/2 + 2*i - 0.1, -panel/2, z0 - 0.05]) cube([0.2, panel, 0.1]);
        translate([-panel/2, -panel/2 + 2*i - 0.1, z0 - 0.05]) cube([panel, 0.2, 0.1]);
    }
}

// design orientation -> standing on its wedge foot: front towards +Y, leaning back by tilt, base on z = 0
module standing() {
    translate([0, 0, (half_out + wedge) * cos(tilt) - lip * sin(tilt)])
        rotate([tilt, 0, 0]) rotate([90, 0, 0]) children();
}

module table_top() { color("burlywood") translate([-260, -170, -4]) cube([520, 340, 4]); }

// ---------------- output selection ----------------
// print: lay the inclined back face flat on the bed
module print_orient() { translate([0, 0, z_mid * cos(back_ang)]) rotate([180 + back_ang, 0, 0]) children(); }

if (part == "shell") shell();
if (part == "print") print_orient() shell();
if (part == "assembly") { shell(); ghosts(); }
if (part == "section_ad") intersection() { union() { shell(); ghosts(); }
                                           translate([usb[0][0] - 200, -200, -100]) cube([200, 400, 200]); }
if (part == "section_win") intersection() { union() { shell(); ghosts(); }
                                            translate([-200, ad_ctr[1], -100]) cube([400, 400, 200]); }
if (part == "section_y") intersection() { union() { shell(); ghost_panel(); ghost_chip(); }
                                          translate([-200, -200, -100]) cube([400, 200, 200]); }
if (part == "section_enc") intersection() { union() { shell(); ghosts(); }
                                            translate([enc_pos[0][0], -200, -100]) cube([200, 400, 200]); }
if (part == "section_edge") intersection() { union() { shell(); ghost_panel(); }
                                             translate([section_edge_x - 200, -200, -100]) cube([200, 400, 200]); }
if (part == "product") { standing() { shell(); ghosts(); ghost_face(); } table_top(); }
if (part == "frame") { ghost_panel(notch = false); ghost_chip(); if (adapters) color("red", 0.8) notch_block(); }   // red = cut this away
if (part == "insert") insert_print();

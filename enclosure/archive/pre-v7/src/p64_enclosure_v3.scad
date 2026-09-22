// =====================================================================
//  p64_enclosure_v3.scad
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
//  v3 (2026-09-11): recessed plinth. Leaning back by tilt drops the
//  shell's back-bottom edge (depth_bottom + lip) * tan(tilt) = 7.2 mm
//  below the front edge, so a flat base needs that much material added
//  below the front outline (v1/v2: a 9 mm bar under the LEDs, wall
//  included). v3 keeps the 2 mm rim on all four sides of the front and
//  starts the wedge at the frame's back face instead (plinth = true):
//  a 4.7 mm plinth set 12 mm behind the front face. The front lip
//  floats 7 mm above the table; the base plane and the lean are those
//  of v2, the contact patch is 22 mm deep instead of 34.
//  plinth = false gives the v2 shape.
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
//  part = "assembly"    -> shell + translucent panel / chip / plug / encoder mock-ups
//  part = "section_x"   -> cut through the cable pocket
//  part = "section_y"   -> cut through the side screw bosses
//  part = "section_enc" -> cut through the right-hand encoder
//  part = "product"     -> the assembled display standing on a table (LED face mock-up)
// =====================================================================

part = "shell";            // "shell" | "print" | "assembly" | "section_x" | "section_y" | "section_enc" | "product"

/* [Panel] */
panel      = 127.8;        // frame outer size, measured in RGB-Matrix-P2-64x64-2D.dwg (Waveshare quotes 128)
panel_clr  = 0.3;          // clearance per side between panel and pocket
frame_d    = 12;           // depth of the plastic frame behind the LED PCB
lip        = 12;           // how far the walls wrap forward over the frame (12 = frame only)
panel_rot  = 90;           // panel rotation inside the shell, CCW seen from the back
// M3 inserts in the panel's own orientation (arrows up), from RGB-Matrix-P2-64x64-2D.dwg
holes_native    = [[0,56.85],[0,-56.85],[56.85,44],[-56.85,44],[56.85,-44],[-56.85,-44]];
hub75_in_native = [-35.0, 5.4];   // centre of the HUB75 IN header, panel orientation, seen from the back

/* [Shell] */
wall         = 2.0;    // side wall thickness
back_t       = 2.4;    // back wall thickness (perpendicular to the back face)
depth_bottom = 22;     // outer depth behind the frame back face at the bottom edge
depth_top    = 8;      // outer depth at the top edge (>= 8 keeps the top screw heads recessed)
tilt         = 12;     // lean-back angle in degrees
plinth       = true;   // v3: wedge starts at plinth_z0 instead of below the front outline (false = v1/v2 bar)
plinth_z0    = 0;      // where the plinth's vertical front face sits: 0 = the frame's back face (lip depth behind the LEDs)
r_in         = 0.6;    // pocket corner radius (the frame corners are sharp; keep small)
ledge_w      = 1.6;    // seating ledge at the frame back face (0 = none); the frame's outer wall is 1.6 mm
ledge_t      = 1.5;

/* [Screws] */
screw_len  = 10;   // M3 screw length
engage     = 5;    // thread engagement inside the brass insert
screw_hole = 3.4;  // clearance hole
cb_d       = 6.5;  // counterbore for head and driver
boss_od    = 10.5; // leaves 2.0 mm walls around the counterbore (JLC3DP preference)
web_t      = 2;    // rib joining each boss to the nearest wall

/* [Controller] */
z_chip      = 0.5;              // chip PCB back face above the frame back face; the chip edge overhangs the
                                // frame rim by ~0.8 mm, so it must rest at or just above the rim (0..1 mm)
chip_size   = [50.01, 42];
chip_socket = [17.69, 10.54];   // HUB75 socket centre from the chip's top-left corner
chip_t      = 1.6;
chip_comp_h = 5.6;
usb_yc      = [9.86, 22.6];     // USB-C port centres measured from the chip's top edge (POWER, USB)
usb_w       = 8.94;
boot_c      = [22.34, 3.24];    // button centres from the chip's top-left corner
rst_c       = [27.39, 3.24];
mic1_c      = [2.2, 30.7];      // microphones (from the product photo, +-1 mm)
mic2_c      = [47.8, 30.7];

/* [Cable] */
pocket_w = 30;          // plug pocket width, covers both USB-C ports with +-1 mm header-position slack
pocket_z = [-2, 10.5];  // plug pocket z-range
groove_w = 12;          // cable groove under the base (wide enough for either port)
notch_w  = 14;          // cable exit notch at the bottom of the back wall
notch_h  = 6;

/* [Back wall features] */
vents           = true;
vent_w          = 1.6;
vent_pitch      = 6;
vent_xmax       = 42;
vent_bands      = [[28, 40], [-28, 36]];   // [centre y, slot length]; lower band shorter to clear the mic holes
vent_skip_lower = [-18, -12];   // columns left out of the lower band for BOOT/RST
pin_d      = 3.0;   // pin holes over BOOT / RESET (+-0.75 mm header-position slack, 2 mm bridge between them)
mic_d      = 3.5;   // microphone holes
// groove marks beside the pin holes instead of text: one groove = BOOT, two = RESET
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
r_out    = r_in + wall;
back_ang = atan((depth_bottom - depth_top) / (2*half_out));   // slope of the back face
z_mid    = (depth_bottom + depth_top) / 2;                     // back face height at y = 0
back_tz  = back_t / cos(back_ang);                             // back wall thickness measured along Z
function z_back(y) = z_mid - y * tan(back_ang);                // outer back face
function z_cav(y)  = z_back(y) - back_tz;                      // cavity back face
wedge_f  = plinth ? 0 : (depth_bottom + lip) * tan(tilt);     // bar below the front outline (v1/v2)
wedge_p  = plinth ? (depth_bottom - plinth_z0) * tan(tilt) : 0; // plinth height at its front face
wedge    = max(wedge_f, wedge_p);                              // how far the shell reaches below the pocket outline
lip_gap  = (depth_bottom + lip) * tan(tilt) - wedge_f;         // base plane to lip bottom at the front edge (in Y)
floor_t  = screw_len - engage;                                 // boss floor between screw head and panel

function rot2(p, a) = [p[0]*cos(a) - p[1]*sin(a), p[0]*sin(a) + p[1]*cos(a)];
function contains(v, x) = len([for (e = v) if (abs(e - x) < 1e-6) e]) > 0;

holes           = [for (h = holes_native) rot2(h, panel_rot)];
chip_org_native = [hub75_in_native[0] - chip_socket[0], hub75_in_native[1] + chip_socket[1]];
function chip_pt(xc, yc) = rot2([chip_org_native[0] + xc, chip_org_native[1] - yc], panel_rot);

boot = chip_pt(boot_c[0], boot_c[1]);
rst  = chip_pt(rst_c[0],  rst_c[1]);
mic1 = chip_pt(mic1_c[0], mic1_c[1]);
mic2 = chip_pt(mic2_c[0], mic2_c[1]);
usb  = [for (y = usb_yc) chip_pt(0, y)];
pocket_xc = (usb[0][0] + usb[1][0]) / 2;
chip_top_y = max([for (c = [[0,0],[chip_size[0],0],chip_size,[0,chip_size[1]]]) chip_pt(c[0], c[1])[1]]);

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
function enc_blocks(x, y0, l) = encoders && len([for (p = enc_pos)
    if (abs(x - p[0])  < enc_board/2 + enc_keepout + vent_w/2 &&
        abs(y0 - p[1]) < enc_board/2 + enc_keepout + l/2) 1]) > 0;

echo(str("outer size X x Y (front) = ", 2*half_out, " x ", 2*half_out + wedge_f,
         "  depth bottom/top = ", depth_bottom + lip, "/", depth_top + lip, "  back slope = ", back_ang, " deg"));
if (plinth) echo(str("plinth: front face at z = ", plinth_z0, ", height ", wedge_p, "  front lip floats ", lip_gap * cos(tilt),
                     " above the table at the front edge, ", wedge_p * cos(tilt), " at the plinth  contact patch ",
                     (depth_bottom - plinth_z0) / cos(tilt), " deep (v2: ", (depth_bottom + lip) / cos(tilt), ")"));
echo(str("clear depth: bottom edge ", z_cav(-half_in), "  chip top edge (y=", chip_top_y, ") ", z_cav(chip_top_y),
         "  y=0 ", z_cav(0), "  top edge ", z_cav(half_in), "  wedge = ", wedge));
echo(str("holes = ", holes, "  top boss back face z = ", z_back(max([for (h = holes) h[1]]))));
echo(str("chip corners = ", chip_pt(0,0), " ", chip_pt(chip_size[0],0), " ", chip_pt(chip_size[0],chip_size[1]), " ", chip_pt(0,chip_size[1])));
echo(str("usb ports = ", usb, "  boot = ", boot, "  rst = ", rst, "  mic1 = ", mic1, "  mic2 = ", mic2));
if (encoders) for (p = enc_pos) {
    echo(str("encoder at ", p, ": cavity depth ", z_cav(p[1]), "  board bottom z ", enc_global_z(p, enc_z_bottom),
             "  peg tips z ", enc_global_z(p, enc_z_peg), " (ledge top ", ledge_t + ledge_w, ", frame back 0)",
             "  board edge to side wall ", half_in - abs(p[0]) - enc_board/2,
             "  post to nearest boss ", enc_boss_gap(p)));
    echo(str("encoder at ", p, ": bushing proud of face ", enc_bush_l - back_t, "  thread for washer+nut ",
             enc_bush_l - (back_t - enc_spot_t), " (needs ", enc_nut_h, ")  shaft proud of face ", enc_shaft_l - back_t,
             "  knob end behind frame back ", z_back(p[1]) + (enc_nut_h - enc_spot_t + knob_h) * cos(back_ang)));
}

// ---------------- primitives ----------------
module rrect(w, h, r) { offset(r = r) square([w - 2*r, h - 2*r], center = true); }

// local frame lying on the outer back face at height y: XY in the face, +Z outwards
module on_back(y) { translate([0, y, z_back(y)]) rotate([-back_ang, 0, 0]) children(); }

module back_slab(th = 0.01) {          // thin slab whose top face is the inclined back plane
    H = 2*half_out / cos(back_ang);
    translate([0, 0, z_mid]) rotate([-back_ang, 0, 0]) translate([0, 0, -th])
        linear_extrude(th) rrect(2*half_out, H, r_out);
}

module outer_body() {
    union() {
        hull() {                            // the shell: front outline (plus the v1/v2 bar if no plinth) to the back face
            translate([0, -wedge_f/2, -lip])
                linear_extrude(0.01) rrect(2*half_out, 2*half_out + wedge_f, r_out);
            back_slab();
        }
        if (plinth) hull() {                // the plinth: same outline dropped by wedge_p at z = plinth_z0, to the back face
            translate([0, -wedge_p/2, plinth_z0])
                linear_extrude(0.01) rrect(2*half_out, 2*half_out + wedge_p, r_out);
            back_slab();
        }
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
}

module plug_pocket() {
    y0 = -half_out - wedge - 1;
    y1 = -half_in + ledge_w + 0.5;
    translate([pocket_xc - pocket_w/2, y0, pocket_z[0]])
        cube([pocket_w, y1 - y0, pocket_z[1] - pocket_z[0]]);
}

module cable_groove() {
    y0 = -half_out - wedge - 1;
    y1 = -half_in + 2;
    translate([pocket_xc - groove_w/2, y0, pocket_z[1] - 0.5])
        cube([groove_w, y1 - y0, depth_bottom - pocket_z[1] + 1.5]);
}

module back_notch() {
    z0 = z_cav(-half_out) - 2;
    translate([pocket_xc - notch_w/2, -half_out - 1, z0])
        cube([notch_w, notch_h + 1, depth_bottom + 1 - z0]);
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
            ledge();
            intersection() { union() { for (p = holes) boss(p); } outer_body(); }
            if (encoders) for (p = enc_pos) enc_posts(p);
        }
        for (p = holes) boss_hole(p);
        plug_pocket();
        cable_groove();
        back_notch();
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
module ghost_panel() {
    hin = rot2(hub75_in_native, panel_rot);
    color("dimgray", 0.35) difference() {
        translate([0, 0, -frame_d]) linear_extrude(frame_d) square(panel, center = true);
        translate([0, 0, -frame_d + 2]) linear_extrude(frame_d) square(105, center = true);
    }
    color("darkgreen", 0.35) translate([0, 0, -frame_d - 2.5]) linear_extrude(2.5) square(127.8, center = true);
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
    color("silver", 0.7) for (y = usb_yc) chip_box(0, y - usb_w/2, 7.2, usb_w, z_chip + chip_t, 3.2);      // USB-C receptacles
    color("gray", 0.6)   chip_box(31.65, 7.14, 15.8, 17.1, z_chip + chip_t, 3.2);                          // ESP32 module
    color("white", 0.6)  chip_box(27, 36.5, 15, 5.5, z_chip + chip_t, chip_comp_h);                       // SPK + GPIO headers
    color("black", 0.6)  chip_box(chip_socket[0] - 4.45, chip_socket[1] - 10.15, 8.9, 20.3, z_chip - 8.5, 8.5);  // HUB75 socket
}

module ghost_plug() {   // right-angle USB-C plug in the POWER port, cable towards the back
    p = usb[0];
    zc = z_chip + chip_t + 1.6;
    color("black", 0.5) translate([p[0] - 6, p[1] - 12, zc - 3.25]) cube([12, 12, 6.5]);
    color("black", 0.5) translate([p[0], p[1] - 8.5, zc]) cylinder(d = 3.5, h = depth_bottom + 15 - zc);
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

module ghosts() { ghost_panel(); ghost_chip(); ghost_plug(); if (encoders) for (i = [0 : len(enc_pos) - 1]) ghost_encoder(enc_pos[i], enc_rot[i]); }

// ---------------- product view: the assembled display standing on a table ----------------
module ghost_face() {   // the LED mask: matt black front with a faint 2 mm pixel grid
    z0 = -frame_d - 2.5 - 1.0;
    color("black") translate([0, 0, z0]) linear_extrude(1.0) square(2*half_in, center = true);   // covers the fit gap in the mock-up
    color([0.2, 0.2, 0.2]) for (i = [1 : 63]) {
        translate([-panel/2 + 2*i - 0.1, -panel/2, z0 - 0.05]) cube([0.2, panel, 0.1]);
        translate([-panel/2, -panel/2 + 2*i - 0.1, z0 - 0.05]) cube([panel, 0.2, 0.1]);
    }
}

// design orientation -> standing on its base: front towards +Y, leaning back by tilt, base on z = 0
// (the lowest point is the front-bottom edge of the bar, or of the plinth)
module standing() {
    translate([0, 0, plinth ? (half_out + wedge_p) * cos(tilt) - plinth_z0 * sin(tilt)
                            : (half_out + wedge_f) * cos(tilt) - lip * sin(tilt)])
        rotate([tilt, 0, 0]) rotate([90, 0, 0]) children();
}

module table_top() { color("burlywood") translate([-260, -170, -4]) cube([520, 340, 4]); }

// ---------------- output selection ----------------
// print: lay the inclined back face flat on the bed
module print_orient() { translate([0, 0, z_mid * cos(back_ang)]) rotate([180 + back_ang, 0, 0]) children(); }

if (part == "shell") shell();
if (part == "print") print_orient() shell();
if (part == "assembly") { shell(); ghosts(); }
if (part == "section_x") intersection() { union() { shell(); ghosts(); }
                                          translate([pocket_xc - 200, -200, -100]) cube([200, 400, 200]); }
if (part == "section_y") intersection() { union() { shell(); ghost_panel(); ghost_chip(); }
                                          translate([-200, -200, -100]) cube([400, 200, 200]); }
if (part == "section_enc") intersection() { union() { shell(); ghosts(); }
                                            translate([enc_pos[0][0], -200, -100]) cube([200, 400, 200]); }
if (part == "product") { standing() { shell(); ghosts(); ghost_face(); } table_top(); }

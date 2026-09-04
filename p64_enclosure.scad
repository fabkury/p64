// =====================================================================
//  p64_enclosure.scad
//  Tabletop back shell for the Waveshare RGB-Matrix-P2-64x64-B
//  (128 x 128 mm LED matrix, 6x M3 brass inserts) with the
//  ESP32-S3-RGB-Matrix controller plugged into the HUB75 IN header.
//  Needs OpenSCAD 2021.01 or newer.
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
//  part = "print"     -> back face on the bed, no supports needed
//  part = "assembly"  -> shell + translucent panel / chip / plug mock-ups
//  part = "section_x" -> cut through the cable pocket
//  part = "section_y" -> cut through the side screw bosses
// =====================================================================

part = "shell";            // "shell" | "print" | "assembly" | "section_x" | "section_y"

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
r_in         = 0.6;    // pocket corner radius (the frame corners are sharp; keep small)
ledge_w      = 1.6;    // seating ledge at the frame back face (0 = none); the frame's outer wall is 1.6 mm
ledge_t      = 1.5;

/* [Screws] */
screw_len  = 10;   // M3 screw length
engage     = 5;    // thread engagement inside the brass insert
screw_hole = 3.4;  // clearance hole
cb_d       = 6.5;  // counterbore for head and driver
boss_od    = 10;
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
pin_d      = 3.5;   // pin holes over BOOT / RESET (sized for +-1 mm header-position error)
mic_d      = 3.5;   // microphone holes
label_d    = 0.6;   // debossed label depth
label_size = 2.4;

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
wedge    = (depth_bottom + lip) * tan(tilt);                   // extra height of the wedge at the front bottom
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

echo(str("outer size X x Y (front) = ", 2*half_out, " x ", 2*half_out + wedge,
         "  depth bottom/top = ", depth_bottom + lip, "/", depth_top + lip, "  back slope = ", back_ang, " deg"));
echo(str("clear depth: bottom edge ", z_cav(-half_in), "  chip top edge (y=", chip_top_y, ") ", z_cav(chip_top_y),
         "  y=0 ", z_cav(0), "  top edge ", z_cav(half_in), "  wedge = ", wedge));
echo(str("holes = ", holes, "  top boss back face z = ", z_back(max([for (h = holes) h[1]]))));
echo(str("chip corners = ", chip_pt(0,0), " ", chip_pt(chip_size[0],0), " ", chip_pt(chip_size[0],chip_size[1]), " ", chip_pt(0,chip_size[1])));
echo(str("usb ports = ", usb, "  boot = ", boot, "  rst = ", rst, "  mic1 = ", mic1, "  mic2 = ", mic2));

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
    hull() {
        translate([0, -wedge/2, -lip])
            linear_extrude(0.01) rrect(2*half_out, 2*half_out + wedge, r_out);
        back_slab();
    }
}

module below_cavity_back() {           // half-space under the cavity back plane
    translate([0, 0, z_mid - back_tz]) rotate([-back_ang, 0, 0])
        translate([-500, -500, -1000]) cube([1000, 1000, 1000]);
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
        if (!(b[0] < 0 && contains(vent_skip_lower, x))) vent_slot(x, b[0], b[1]);
}

module back_hole(p, d) {
    on_back(p[1]) translate([p[0], 0, -back_tz - 1]) cylinder(d = d, h = back_tz + 2);
}

module label(txt, p, dx) {
    on_back(p[1]) translate([p[0] + dx, 0, -label_d]) linear_extrude(label_d + 1)
        text(txt, size = label_size, halign = "right", valign = "center", font = "Liberation Sans:style=Bold");
}

// ---------------- the part ----------------
module shell() {
    difference() {
        union() {
            difference() { outer_body(); cavity(); }
            ledge();
            intersection() { union() { for (p = holes) boss(p); } outer_body(); }
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
        label("BOOT", boot, -(pin_d/2 + 1));
        label("RST",  rst,  -(pin_d/2 + 1));
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

// ---------------- output selection ----------------
// print: lay the inclined back face flat on the bed
module print_orient() { translate([0, 0, z_mid * cos(back_ang)]) rotate([180 + back_ang, 0, 0]) children(); }

if (part == "shell") shell();
if (part == "print") print_orient() shell();
if (part == "assembly") { shell(); ghost_panel(); ghost_chip(); ghost_plug(); }
if (part == "section_x") intersection() { union() { shell(); ghost_panel(); ghost_chip(); ghost_plug(); }
                                          translate([pocket_xc - 200, -200, -100]) cube([200, 400, 200]); }
if (part == "section_y") intersection() { union() { shell(); ghost_panel(); ghost_chip(); }
                                          translate([-200, -200, -100]) cube([400, 200, 200]); }

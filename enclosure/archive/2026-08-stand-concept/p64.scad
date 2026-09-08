// ============================================================
// p64 — LED-matrix frame: desktop stand + flush wall mount
// ============================================================
// Hardware:
//   * Waveshare RGB-Matrix-P2-64x64 LED panel (128 x 128 x 14.5 mm)
//       https://www.waveshare.com/wiki/RGB-Matrix-P2-64x64
//   * Waveshare ESP32-S3-RGB-Matrix driver (50.01 x 42.00 mm PCB)
//       plugged onto the panel's HUB75E input connector,
//       with its two USB-C ports facing DOWN.
//
// Printed parts (set `part` below or via -D on the CLI):
//   part = "frame"    -> main enclosure  (print with BACK face on the bed)
//   part = "stand"    -> detachable desk stand (print lying on its side)
//   part = "fitcheck" -> 4 mm front slice of the frame, to verify the
//                        panel's side fit before committing to the big print
//   part = "assembly" -> visual: unit on its stand, tilted 12 deg on a desk
//   part = "print"    -> frame + stand laid out in print orientation
//
// BOM (besides the two printed parts):
//   * 4x M3 x 12 machine screws  (thread into the panel's M3 inserts)
//   * wall mode: 2x wall screws, head dia 7-8 mm, shank <= 4 mm,
//     driven on 80 mm horizontal centers (keyhole hangers)
//
// Assembly:
//   1. Plug the driver onto the panel's HUB75 input (USB-C down),
//      wire panel power (VH4) to the driver's 5V/GND.
//   2. Slide the panel+driver into the frame from the FRONT.
//   3. Drop 4x M3x12 into the 4 counterbored holes in the back and
//      drive them into the panel's inserts with a long screwdriver.
//   4. Desk: slide the frame down onto the stand's T-rail.
//      Wall: no stand; hang the keyholes on two screws.
//
// !!! MEASURE BEFORE PRINTING !!!
//   The values in the "MEASURE ME" block are estimated from product
//   photos. Verify with calipers / a dry fit and adjust:
//     - hole_dx, hole_dy : spacing of the panel's M3 inserts
//     - drv_cx, drv_cy   : driver PCB centre, measured on the real
//                          assembly, looking at the BACK, from panel centre
//     - drv_standoff     : panel back -> driver PCB back, connector mated
//   Print part="fitcheck" first (cheap) to confirm the panel pocket.
//
// Coordinates: looking at the BACK of the unit: +x right, +y up,
// +z toward the viewer. The panel's front (LED) face is z = 0.
// ============================================================

/* ------------------------ what to render ------------------- */
part = "assembly";   // ["assembly", "frame", "stand", "fitcheck", "print"]
show_hardware = true;   // show panel/driver mock-ups in the assembly view
show_stand    = true;   // show the stand in the assembly view

/* ------------------------ MEASURE ME ----------------------- */
hole_dx      = 113;     // panel M3 insert spacing, horizontal  << MEASURE
hole_dy      = 90;      // panel M3 insert spacing, vertical    << MEASURE
drv_cx       = -34.7;   // driver PCB centre x (from back)      << MEASURE
drv_cy       = 10;      // driver PCB centre y                  << MEASURE
drv_standoff = 10;      // panel back -> driver PCB back, mated << MEASURE

/* ------------------------ panel ---------------------------- */
panel_w   = 128;        // panel is square
panel_t   = 14.5;
panel_clr = 0.3;        // pocket clearance per side

/* ------------------------ driver (after USB-down rotation) - */
drv_w   = 42;           // x extent once rotated USB-edge-down
drv_h   = 50;           // y extent
drv_pcb = 1.6;
// feature offsets from the driver PCB centre (derived from the
// board photos; positions rotate with drv_cx/drv_cy):
usb1_dx    = -11;       // USB-C "POWER" port
usb2_dx    = 2.4;       // USB-C "USB" port
sd_dx      = 13;   sd_dy   = -13;   // microSD holder (front face)
btn_dx     = -16.2; btn_dy = 1.15;  // midpoint of BOOT/RESET buttons

/* ------------------------ enclosure ------------------------ */
wall    = 2.7;          // side wall thickness
back_t  = 3;            // back wall thickness
cavity  = 19;           // panel back -> back wall interior
corner_r = 3;           // outer corner radius
front_ch = 1.2;         // front edge chamfer

/* ------------------------ fasteners ------------------------ */
m3_len        = 12;     // panel screws: M3 x 12
insert_engage = 4.5;    // thread depth used inside the panel inserts

/* ------------------------ wall mount ----------------------- */
kh_x = 40;              // keyholes at (+/-kh_x, kh_y): 80 mm centers
kh_y = 48;

/* ------------------------ desk stand ----------------------- */
tilt      = 12;         // lean-back angle, degrees
stand_w   = 90;         // stand width
rail_cx   = 8;          // T-rail centre x (offset clears USB window)
rail_stem_w = 30;
rail_cap_w  = 44;
rail_clr    = 0.3;      // per-side sliding clearance

/* ------------------------ misc ----------------------------- */
vents = true;
$fa = 3; $fs = 0.4;
eps = 0.01;

/* ------------------------ derived -------------------------- */
inner  = panel_w + 2*panel_clr;           // 128.6
W      = inner + 2*wall;                  // 134.0 outer square
D      = panel_t + cavity + back_t;       // 36.5 total depth
cav_z1 = D - back_t;                      // 33.5 cavity ceiling
pcb_z0 = panel_t + drv_standoff;          // driver PCB back face
pcb_z1 = pcb_z0 + drv_pcb;                // driver PCB front face
head_z = panel_t + m3_len - insert_engage;// M3 head seat depth

usb_win_cx = drv_cx + (usb1_dx + usb2_dx)/2; // USB window centre x
usb_win_w  = 28;
sd_win_c   = [drv_cx + sd_dx, drv_cy + sd_dy];
btn_c      = [drv_cx + btn_dx, drv_cy + btn_dy];

// desk plane relative to the (tilted) enclosure, used by the stand:
// y_desk(z) = lip_top + (z - desk_z0) * tan(tilt)
lip_top  = -W/2 - 0.2;
desk_z0  = D + 13.5;
function desk_y(z) = lip_top + (z - desk_z0) * tan(tilt);

echo(str("p64: outer ", W, " x ", W, " x ", D, " mm"));
echo(str("p64: panel screws M3x", m3_len, ", head seats ", D - head_z,
         " mm down the counterbores"));

/* ============================================================
   2D / shape helpers
   ============================================================ */
module rsq(w, h, r) offset(r) offset(-r) square([w, h], center = true);

// obround (stadium) centered, w across x, l across y
module obround(w, l) hull() {
    translate([0,  (l - w)/2]) circle(d = w);
    translate([0, -(l - w)/2]) circle(d = w);
}

// window through the back wall, rounded corners, centered on c
module back_window(c, w, h, r = 3)
    translate([c[0], c[1], cav_z1 - 3])
        linear_extrude(back_t + 3 + eps) rsq(w, h, r);

// engraved label on the outer back face
module back_label(pos, s, size = 3.5)
    translate([pos[0], pos[1], D - 0.6])
        linear_extrude(0.7)
            text(s, size = size, halign = "center", valign = "center");

/* ============================================================
   frame body
   ============================================================ */
module frame_blank() {
    union() {
        translate([0, 0, front_ch])
            linear_extrude(D - front_ch) rsq(W, W, corner_r);
        hull() {   // front edge chamfer
            linear_extrude(eps)
                rsq(W - 2*front_ch, W - 2*front_ch, max(corner_r - front_ch, 0.8));
            translate([0, 0, front_ch]) linear_extrude(eps) rsq(W, W, corner_r);
        }
    }
}

// screw tubes: back wall -> panel back, at the panel's insert grid
module screw_tubes()
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*hole_dx/2, sy*hole_dy/2, panel_t])
            cylinder(d = 8.4, h = cav_z1 - panel_t + 0.5);

module screw_bores()
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*hole_dx/2, sy*hole_dy/2, 0]) {
            translate([0, 0, panel_t - 1.5]) cylinder(d = 3.4, h = head_z - panel_t + 1.5 + eps);
            translate([0, 0, head_z])        cylinder(d = 6.4, h = D - head_z + eps);
        }

// corner gussets the panel back rests against
module corner_gussets()
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*inner/2, sy*inner/2, panel_t])
            rotate([0, 0, sx*sy > 0 ? (sx > 0 ? 180 : 0) : (sx > 0 ? 90 : -90)])
                linear_extrude(cav_z1 - panel_t + 0.5)
                    polygon([[0, 0], [8, 0], [0, 8]]);

/* --------------------- wall keyholes ----------------------- */
module keyhole_pads()
    for (sx = [-1, 1])
        translate([sx*kh_x, kh_y, D - 6])
            cylinder(d = 17, h = 6 - back_t + 0.5 + eps);

module keyhole_cuts()
    for (sx = [-1, 1])
        translate([sx*kh_x, kh_y, 0]) {
            // entry hole + shank slot: through the 6 mm pad
            translate([0, 0, D - 6.4]) linear_extrude(6.4 + eps) {
                circle(d = 8.6);
                translate([0, 5.5]) obround(4.4, 11);
            }
            // head channel: undercut behind a 2.6 mm outer skin
            translate([0, 0, D - 6.4]) linear_extrude(6.4 - 2.6)
                translate([0, 5.5]) obround(8.6, 11);
        }

/* --------------------- stand T-slot ------------------------ */
// vertical T-slot in the back wall, open at the bottom face,
// closed rounded top. Slot spans y = -85 (past the bottom face,
// i.e. open) up to a tip at y = -24. Cap channel sits deeper.
module tslot_cut() {
    translate([rail_cx, -54.5, 0]) {
        // throat (at the outer surface)
        translate([0, 0, D - 1.55]) linear_extrude(1.55 + eps)
            rsq(rail_stem_w + 2*rail_clr, 61, 6.3);
        // cap channel
        translate([0, 0, D - 3.05]) linear_extrude(1.5)
            rsq(rail_cap_w + 2*rail_clr, 61, 6.3);
    }
}

/* --------------------- vents ------------------------------- */
module vent_cuts()
    for (vx = [10:6:44], row = [[-7.5, 15], [11, 16], [28, 14]])
        translate([vx, row[0], cav_z1 - 1])
            linear_extrude(back_t + 1 + eps) obround(2.6, row[1]);

/* --------------------- the frame --------------------------- */
module frame() {
    difference() {
        union() {
            difference() {
                frame_blank();
                // panel pocket + electronics cavity (small corner
                // radius so the panel's square corners still fit)
                translate([0, 0, -eps])
                    linear_extrude(cav_z1 + eps) rsq(inner, inner, 0.8);
            }
            screw_tubes();
            corner_gussets();
            keyhole_pads();
            // reinforcement pad behind the T-slot
            translate([rail_cx - rail_cap_w/2 - 4, -W/2 + wall, D - 6])
                cube([rail_cap_w + 8, -21 - (-W/2 + wall), 6 - back_t + 0.5]);
        }
        screw_bores();
        keyhole_cuts();
        tslot_cut();

        // USB-C window (bottom wall): both ports + plug overmolds
        translate([usb_win_cx, -W/2 - eps, 0]) rotate([-90, 0, 0])
            translate([0, 0, -eps]) linear_extrude(wall + 2)
                translate([0, -(23.5 + 34)/2]) rsq(usb_win_w, 34 - 23.5, 2);

        // microSD service window (back wall, over the push-push slot)
        back_window(sd_win_c, 20, 16);

        // BOOT + RESET access (back wall)
        translate([btn_c[0], btn_c[1], cav_z1 - 3])
            linear_extrude(back_t + 3 + eps) obround(9, 17);

        if (vents) vent_cuts();

        // engravings
        back_label([30, -52], "p64", 9);
        back_label([btn_c[0], btn_c[1] - 12.5], "BOOT/RST", 3);
        back_label([sd_win_c[0], sd_win_c[1] - 12], "SD", 3);
    }
}

/* ============================================================
   desk stand
   ============================================================ */
module stand() {
    riser_f = D + 0.2;        // riser front face (0.2 off the back wall)
    riser_b = riser_f + 5.5;
    riser_top = -22;
    z_tip  = 4;               // front lip tip
    z_rear = D + 35.5;        // rear foot tip

    difference() {
        union() {
            // main body: side profile extruded across the width
            translate([stand_w/2, 0, 0]) rotate([0, -90, 0])
                linear_extrude(stand_w)
                    polygon([
                        [z_tip,   lip_top],
                        [riser_f, lip_top],
                        [riser_f, riser_top],
                        [riser_b, riser_top],
                        [z_rear,  desk_y(z_rear)],
                        [z_tip,   desk_y(z_tip)]
                    ]);
            // T-rail on the riser front face: rail tips y -63..-27,
            // stem runs the full depth into the riser, cap is the
            // wide retaining flange riding in the deeper channel
            translate([rail_cx, -45, 0]) {
                translate([0, 0, D - 3.05 + 0.2])
                    linear_extrude(riser_f - (D - 3.05 + 0.2) + eps)
                        rsq(rail_stem_w, 36, 6);
                translate([0, 0, D - 3.05 + 0.2]) linear_extrude(1.0)
                    rsq(rail_cap_w, 36, 6);
            }
        }
        // cable channel: down from the USB window, then rearward
        // along the desk, exiting behind the stand
        translate([usb_win_cx - 7, -85, 23]) cube([14, 25, 14]);
        translate([usb_win_cx - 7, desk_y(30) - 2, 30])
            rotate([-tilt, 0, 0]) cube([14, 8, 55]);
    }
}

/* ============================================================
   hardware mock-ups (visual only)
   ============================================================ */
module panel_mock() color("#303030") {
    translate([0, 0, panel_t/2]) cube([panel_w, panel_w, panel_t], center = true);
    // HUB75 input / output shrouded headers
    for (hx = [-41, 41])
        translate([hx, 0, panel_t + 4.8]) cube([9, 22, 9.6], center = true);
    // VH4 power socket
    translate([0, 0, panel_t + 3.5]) cube([14, 7, 7], center = true);
}

module driver_mock() translate([drv_cx, drv_cy, 0]) {
    color("#1a5fb4") translate([0, 0, (pcb_z0 + pcb_z1)/2])
        cube([drv_w, drv_h, drv_pcb], center = true);
    color("#c0c0c0") {
        // USB-C ports on the bottom edge
        for (ux = [usb1_dx, usb2_dx])
            translate([ux, -drv_h/2 + 3, pcb_z1 + 1.6]) cube([9, 8, 3.2], center = true);
        // microSD holder + ESP32 module
        translate([sd_dx, sd_dy, pcb_z1 + 1]) cube([14, 15, 2], center = true);
        translate([0, 12, pcb_z1 + 1.6]) cube([18, 26, 3.2], center = true);
        // BOOT / RESET buttons
        for (by = [-2.75, 2.75])
            translate([btn_dx, btn_dy + by, pcb_z1 + 1]) cylinder(d = 3.4, h = 2);
    }
}

/* ============================================================
   part selection
   ============================================================ */
module assembly() {
    lift = -(desk_y(desk_z0)*cos(tilt) - desk_z0*sin(tilt)); // desk -> y=0
    translate([0, lift, 0]) rotate([tilt, 0, 0]) {
        color("#e8e4da") frame();
        if (show_stand) color("#d08838") stand();
        if (show_hardware) { panel_mock(); driver_mock(); }
    }
    color("#888888", 0.25) translate([0, -1, 15])
        cube([260, 2, 170], center = true);   // desk surface
}

module fitcheck()
    intersection() {
        frame();
        translate([0, 0, -1]) linear_extrude(5) square(W + 2, center = true);
    }

if (part == "frame")      frame();
else if (part == "stand") stand();
else if (part == "fitcheck") fitcheck();
else if (part == "print") {
    translate([0, 0, D]) rotate([180, 0, 0]) frame();        // back on bed
    translate([W/2 + 60, 45, stand_w/2]) rotate([0, 90, 0]) stand(); // on side
}
else if (part == "assembly")
    rotate([90, 0, 0]) assembly();    // remap y-up model to z-up view

// pieces.scad: exports the p64b assembly one part at a time, for the concept video.
//
// Includes the enclosure source (enclosure/src/p64_enclosure_v7.scad) with encoders on and
// selects one piece with -D piece="...". Every piece comes out in the source's design
// coordinates (origin = centre of the panel frame's back face, +Z into the shell), so the
// STLs assemble in Blender without any per-part offsets. The geometry of the mock-ups is
// the source's own (ghost_* modules); the pieces that the source draws in several colours
// inside one module are split here so each colour can get its own material.
//
//   openscad -o build/insert.stl -D piece=\"insert\" docs/video/pieces.scad
//   openscad -o build/values.echo -D piece=\"values\" docs/video/pieces.scad   (the numbers Blender needs)
//
// The shell itself is not exported here: docs/video/scene.py uses the committed
// enclosure/output/p64b/v7b/p64_enclosure_print.stl and undoes print_orient() (the values
// come from the "values" echo).

include <../../enclosure/src/p64_enclosure_v7.scad>
encoders = true;
part = "none";          // silence the source's own output selection
piece = "values";

// ---- panel (ghost_panel split by colour) ----
module pc_frame() {
    difference() {
        translate([0, 0, -frame_d]) linear_extrude(frame_d) square(panel, center = true);
        translate([0, 0, -frame_d + 2]) linear_extrude(frame_d - 2 - frame_plate_t + 0.01) square(panel - 2*frame_wall_t, center = true);
        translate([0, 0, -frame_plate_t - 0.01]) linear_extrude(frame_plate_t + 1) rotate(panel_rot) frame_opening2d();
        if (adapters && ghost_notch) notch_block();
    }
}
module pc_board() { translate([0, 0, -frame_d - panel_stack]) linear_extrude(panel_stack - 1.0) square(board, center = true); }
module pc_mask()  { translate([0, 0, -frame_d - panel_stack]) linear_extrude(1.0) square(board, center = true); }
module pc_hub75() { hin = rot2(hub75_in_native, panel_rot);
                    translate([hin[0], hin[1], -frame_d]) linear_extrude(8.9) rotate(panel_rot) square([8.9, 20.3], center = true); }
module pc_pwr()   { pwr = rot2([12.3, 5.3], panel_rot);
                    translate([pwr[0], pwr[1], -frame_d]) linear_extrude(13) rotate(panel_rot) square([8, 16], center = true); }

// ---- controller (ghost_chip split by colour) ----
module pc_chip_pcb()     { chip_box(0, 0, chip_size[0], chip_size[1], z_chip, chip_t); }
module pc_chip_usb()     { for (y = usb_yc) chip_box(0, y - usb_w/2, 7.2, usb_w, z_chip + chip_t, usb_h); }
module pc_chip_module()  { chip_box(31.65, 7.14, 15.8, 17.1, z_chip + chip_t, 3.2); }
module pc_chip_headers() { chip_box(27, 36.5, 15, 5.5, z_chip + chip_t, chip_comp_h); }
module pc_chip_socket()  { chip_box(chip_socket[0] - 4.45, chip_socket[1] - 10.15, 8.9, 20.3, z_chip - 8.5, 8.5); }

// ---- adapters (ghost_adapters split by colour) ----
module pc_ad_body() { for (i = [0 : 1]) { q = usb[i]; cy = ad_ctr[1];
    difference() {
        translate([q[0], cy, ad_z0]) linear_extrude(ad_len) rrect(ad_w, ad_t, ad_r);
        translate([q[0], cy, ad_z1 - 7]) linear_extrude(7.01) rrect(8.9, 3.2, 1.5);
    } } }
module pc_ad_plug() { for (i = [0 : 1]) { q = usb[i]; cy = ad_ctr[1];
    translate([q[0] - 4.15, cy + ad_t/2 - 0.01, port_z - 1.2]) cube([8.3, ad_gap + 6.5, 2.4]); } }

// ---- encoders (ghost_encoder split by colour; both encoders in one piece) ----
module on_enc(i) { p = enc_pos[i]; on_back(p[1]) translate([p[0], 0, 0]) children(); }
module pc_enc_board()   { for (i = [0 : 1]) on_enc(i) translate([0, 0, enc_z_bottom]) linear_extrude(enc_board_t) rotate(enc_rot[i]) rrect(enc_board, enc_board, enc_board_r); }
module pc_enc_sockets() { for (i = [0 : 1]) on_enc(i) for (s = [-1, 1]) rotate(enc_rot[i]) translate([s*(enc_board/2 - 2.1) - 3, -3, enc_z_board]) cube([6, 6, 2.9]); }
module pc_enc_body()    { for (i = [0 : 1]) on_enc(i) translate([0, 0, enc_z_board]) linear_extrude(enc_body_h) rotate(45) square([12.5, enc_body_w], center = true); }
module pc_enc_metal()   { for (i = [0 : 1]) on_enc(i) { translate([0, 0, -back_t]) cylinder(d = enc_bush_d, h = enc_bush_l);
                                                        translate([0, 0, -back_t]) cylinder(d = enc_shaft_d, h = enc_shaft_l); } }
module pc_enc_nut()     { for (i = [0 : 1]) on_enc(i) translate([0, 0, -enc_spot_t]) cylinder(d = 10 / cos(30), h = enc_nut_h, $fn = 6); }
module pc_enc_knob()    { for (i = [0 : 1]) on_enc(i) translate([0, 0, -enc_spot_t + enc_nut_h + 0.3]) cylinder(d = knob_d, h = knob_h); }

// ---- screws: six M3 x 10 pan heads in the counterbores (mock-up only, not in the source) ----
module pc_screws() { for (p = holes) on_back(p[1]) translate([p[0], 0, 0]) {
    translate([0, 0, -1.5 - 2.4]) cylinder(d = 5.6, h = 2.4);                 // head, 1 mm below the face (README: top bosses)
    translate([0, 0, -1.5 - 2.4 - 10]) cylinder(d = 3, h = 10.01);          // M3 x 10 thread
} }

if (piece == "frame")        pc_frame();
if (piece == "board")        pc_board();
if (piece == "mask")         pc_mask();
if (piece == "hub75")        pc_hub75();
if (piece == "pwr")          pc_pwr();
if (piece == "chip_pcb")     pc_chip_pcb();
if (piece == "chip_usb")     pc_chip_usb();
if (piece == "chip_module")  pc_chip_module();
if (piece == "chip_headers") pc_chip_headers();
if (piece == "chip_socket")  pc_chip_socket();
if (piece == "ad_body")      pc_ad_body();
if (piece == "ad_plug")      pc_ad_plug();
if (piece == "insert")       insert_in_place();
if (piece == "enc_board")    pc_enc_board();
if (piece == "enc_sockets")  pc_enc_sockets();
if (piece == "enc_body")     pc_enc_body();
if (piece == "enc_metal")    pc_enc_metal();
if (piece == "enc_nut")      pc_enc_nut();
if (piece == "enc_knob")     pc_enc_knob();
if (piece == "screws")       pc_screws();
if (piece == "values") {
    echo(VALUES = str("{",
        "\"tilt\":", tilt, ",\"back_ang\":", back_ang, ",\"z_mid\":", z_mid, ",\"half_out\":", half_out,
        ",\"wedge\":", wedge, ",\"lip\":", lip, ",\"frame_d\":", frame_d, ",\"panel_stack\":", panel_stack,
        ",\"board\":", board, ",\"panel\":", panel, ",\"back_t\":", back_t,
        ",\"holes\":", holes, ",\"enc_pos\":", enc_pos, ",\"usb\":", usb, ",\"ad_ctr\":", ad_ctr,
        ",\"z_back_holes\":", [for (p = holes) z_back(p[1])], ",\"z_back_enc\":", [for (p = enc_pos) z_back(p[1])],
        "}"));
}

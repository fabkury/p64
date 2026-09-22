// p64_enclosure_v7a.scad: the p64a shell (solder-less variant, no rotary encoders).
// The geometry lives in p64_enclosure_v7.scad; this file only switches the encoders off.
// The assignment must come AFTER the include: OpenSCAD lets the including file override
// an included file's variables that way (before the include it would be overwritten).
// Outputs: output/p64a/v7a/. Render with the same commands as v7b, this file instead.
include <p64_enclosure_v7.scad>
encoders = false;

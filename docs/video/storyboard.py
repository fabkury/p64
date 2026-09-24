"""The storyboard of the p64b concept video: one place for every time in it.

Read by bake_leds.py (what the panel shows), scene.py (camera and explosion, inside
Blender) and compose.py (captions, fades, the end card). Times are seconds from the start
of the video; frames are 1-based like Blender's.
"""

FPS = 30
DURATION = 30.0
FRAMES = int(DURATION * FPS)            # 900
WIDTH, HEIGHT = 1920, 1080

# --- camera beats (scene.py) --------------------------------------------------------
# Each beat: (t0, t1, azimuth deg, elevation deg, distance m, target z m, target along the
# explosion axis m). The camera eases from one beat's pose to the next; azimuth 0 is the
# front (the LEDs), 180 the back; positive azimuth walks the camera to the device's left
# as seen from the front. The target is the point the camera looks at.
CAMERA = [
    # t0     t1     az    el   dist  tz    ty(explosion axis)
    (0.0,   0.0,   -28,   12,  0.50, 0.066, 0.00),     # front three-quarter, art playing
    (4.0,   4.0,   -34,   14,  0.46, 0.066, 0.00),     # slow push-in ends
    (7.0,   7.0,  -150,   22,  0.50, 0.064, 0.00),     # back three-quarter (over the right shoulder)
    (9.0,   9.0,  -102,   24,  0.62, 0.060, 0.10),     # exploded: from the side and above, the stack reads left to right
    (15.2,  15.2, -122,   18,  0.60, 0.060, 0.11),     # slow orbit along the stack
    (18.6,  18.6,  -24,   11,  0.48, 0.066, 0.00),     # front again
    (27.6,  27.6,  -18,    9,  0.43, 0.064, 0.00),     # final push-in
    (28.4,  28.4,  -18,    9,  0.43, 0.064, 0.00),
]

# --- explosion (scene.py) -------------------------------------------------------------
EXPLODE_T0, EXPLODE_T1 = 7.0, 9.0       # parts fly out
COLLAPSE_T0, COLLAPSE_T1 = 15.2, 17.2   # and come back
# offsets along the design +Z axis (into the shell, i.e. backwards), in mm, per group
EXPLODE_MM = {
    "panel": 0.0,
    "chip": 25.0,
    "adapters": 50.0,
    "insert": 75.0,
    "encoders": 105.0,
    "shell": 140.0,
    "enc_nut": 165.0,
    "enc_knob": 182.0,
    "screws": 205.0,
}
EXPLODE_STAGGER = 0.08                  # s between the groups' starts, front to back

LAST_RENDER_FRAME = int(28.4 * FPS)     # after that the end card is composed without Blender

# --- what the panel shows (bake_leds.py): (t0, t1, source) --------------------------
# sources: "art:<file in docs/video/artworks>" (the user's selection, 2026-09-24; f12030f3 is
# the hero piece and leads), "gif:<file in firmware/tests/host/corpus/gifs>" (v1 and v2 used
# night-light-crane, canopy, paws2025-in-the-stars, lo-fi-glow), "clock", "analogue",
# "weather", "stream"
PANEL = [
    (0.0,  7.0,  "art:f12030f3-d150-4af1-9b16-d3a65a4b1df8.webp"),   # the hero artwork
    (7.0,  18.6, "art:c5170051-2bad-48dd-b242-ad4c804d12f1.webp"),   # seen from behind, then during the reassembly
    (18.6, 20.8, "art:d2afeeba-0b9e-4840-adbc-3c6bc2b47637.webp"),
    (20.8, 21.7, "clock"),
    (21.7, 22.6, "analogue"),
    (22.6, 23.6, "weather"),
    (23.6, 25.6, "stream"),
    (25.6, 28.4, "art:900f7d49-f1a4-4ba4-a54a-88e25de33e8d.webp"),
    (28.4, 30.0, "black"),
]

# --- captions (compose.py): (t0, t1, headline, line 2) ------------------------------
# v1 (2026-09-24, the first cut): twelve captions of 1.5 to 3 s, two lines each. Kept so the
# two versions can be compared; compose.py --captions v1 uses it.
CAPTIONS_V1 = [
    (0.8,  4.0,  "p64: a 64 × 64 pixel-art player for your desk",
                 "128 mm square · USB-C · open source · about $80 in parts"),
    (4.3,  7.0,  "p64b: two rotary encoders on the back",
                 "A: brightness, press to pause · B: next / previous, press to like"),
    (7.3,  9.0,  "Exploded view", "one 3D print around two Waveshare boards"),
    (9.0,  10.5, "Waveshare RGB-Matrix-P2 64 × 64 panel",
                 "4096 LEDs at a 2 mm pitch, 128 × 128 mm"),
    (10.5, 12.0, "ESP32-S3-RGB-Matrix driver board, plugged onto the HUB75 header",
                 "Wi-Fi, IMU, microSD slot, temperature sensor, two USB-C ports"),
    (12.0, 13.5, "Two 90-degree USB-C adapters and a glued cradle insert",
                 "the cables leave through one window in the back face"),
    (13.5, 15.2, "Two Adafruit 5880 rotary encoders, one support-free print, six M3 × 10 screws",
                 "every wall 2 mm or thicker; prints flat on its back face"),
    (15.6, 18.4, "The boards plug together, the shell screws onto the panel",
                 "p64a needs no soldering; p64b adds the two knobs"),
    (18.8, 20.8, "Plays GIF, PNG, WebP and BMP, every frame, none skipped",
                 "from a microSD card and from Makapix Club channels over Wi-Fi"),
    (20.8, 23.6, "A clock, the weather and the room temperature",
                 "10-bit colour at 271 Hz, gamma 2.2, seamless transitions"),
    (23.6, 25.6, "Live pixel streams over UDP (DDP and raw p64)",
                 "mirror an image, an animation or a region of your screen"),
    (25.6, 27.6, "Web UI at p64.local, HTTP API with WebSocket push",
                 "optional PIN, tap to skip, OTA updates from GitHub Releases"),
]
# v2 (the user's request, 2026-09-24: far less text, each caption up for longer): seven
# captions of 3 to 4.6 s, one line each, a short second line on two of them. The default.
CAPTIONS_V2 = [
    (0.8,  4.2,  "p64: a 64 × 64 pixel-art player", "128 mm · USB-C · open source"),
    (4.4,  8.4,  "Two knobs on the back (p64b)", "brightness and pause · next and like"),
    (8.8,  12.0, "Inside: the LED panel and the ESP32-S3 driver board", ""),
    (12.2, 15.2, "One 3D print, two USB-C adapters, six screws", ""),
    (15.6, 18.6, "No soldering for p64a", "p64b adds the two knobs"),
    (18.8, 23.4, "Art from microSD or Makapix Club · clock · weather", ""),
    (23.6, 27.4, "Live streams over Wi-Fi · web UI at p64.local", ""),
]
CAPTIONS = CAPTIONS_V2
CAPTION_FADE = 0.4

FADE_IN = (0.0, 0.8)                    # from black
FADE_OUT = (27.6, 28.4)                 # to black
END_CARD = (28.4, 30.0)                 # logo, repository, licence

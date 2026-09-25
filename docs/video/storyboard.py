"""The storyboard of the p64b concept video: one place for every time in it.

Read by bake_leds.py (what the panel shows), scene.py (camera, explosion and knobs, inside
Blender) and compose.py (captions, the virtual screen, fades, the end card). Times are
seconds from the start of the video; frames are 1-based like Blender's.

This is the v4 timeline (2026-09-24): 50 s, with a look into the shell cavity during the
explosion and the knob scene after the reassembly.
The 30 s timeline of v1 to v3 is in git history (commit d682551).
"""

FPS = 30
DURATION = 50.0
FRAMES = int(DURATION * FPS)            # 1500
WIDTH, HEIGHT = 1920, 1080

# --- camera beats (scene.py) --------------------------------------------------------
# Each beat: (t0, t1, azimuth deg, elevation deg, distance m, target z m, target along the
# explosion axis m, lateral offset m). The camera eases from one beat's pose to the next;
# azimuth 0 is the front (the LEDs), 180 the back; positive azimuth walks the camera to the
# device's left as seen from the front. The target is the point the camera looks at; a
# positive lateral offset slides camera and target to the camera's right, so the device
# sits in the left half of the frame.
CAMERA = [
    # t0     t1     az    el   dist  tz     ty(explosion axis) tx(lateral)
    (0.0,   0.0,   -28,   12,  0.50, 0.066, 0.00, 0.00),     # front three-quarter, art playing
    (4.0,   4.0,   -34,   14,  0.46, 0.066, 0.00, 0.00),     # slow push-in ends
    (7.0,   7.0,  -150,   22,  0.50, 0.064, 0.00, 0.00),     # back three-quarter (over the right shoulder)
    (9.0,   9.0,  -102,   24,  0.62, 0.060, 0.10, 0.00),     # exploded: from the side and above, the stack reads left to right
    (15.2,  15.2, -122,   18,  0.60, 0.060, 0.11, 0.00),     # slow orbit along the stack
    (17.0,  17.0,  -64,   30,  0.58, 0.072, 0.085, 0.00),    # into the shell's cavity: from the front-right, aimed between panel and shell
    (18.8,  18.8,  -50,   27,  0.56, 0.072, 0.085, 0.00),    # slow drift over the cavity
    (20.2,  20.2, -122,   18,  0.60, 0.060, 0.11, 0.00),     # back to the side for the reassembly
    (23.0,  23.0,  158,   12,  0.56, 0.062, 0.00, 0.105),    # the knob scene: the whole back, device in the left half
    (36.6,  36.6,  164,   10,  0.53, 0.062, 0.00, 0.105),    # slow drift while the knobs turn
    (38.6,  38.6,  -24,   11,  0.48, 0.066, 0.00, 0.00),     # front again
    (47.6,  47.6,  -18,    9,  0.43, 0.064, 0.00, 0.00),     # final push-in
    (48.4,  48.4,  -18,    9,  0.43, 0.064, 0.00, 0.00),
]

# --- explosion (scene.py) -------------------------------------------------------------
EXPLODE_T0, EXPLODE_T1 = 7.0, 9.0       # parts fly out
CAVITY = (15.6, 19.8)                   # the camera looks into the shell's cavity; a light in there fades in and out
COLLAPSE_T0, COLLAPSE_T1 = 20.2, 22.2   # and come back (after the look into the cavity)
# offsets along the explosion axis (straight back, horizontal), in mm, per group
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
EXPLODE_STAGGER = 0.08                  # s between the groups' starts

# --- the knob scene (v4) --------------------------------------------------------------
# Knob 0 is the right one seen from the back (design x = +47), knob 1 the left one. The
# hardware docs leave which is A to a settings switch; the video calls knob 0 "A".
KNOB_SCENE = (23.0, 37.0)
VIRTUAL = (23.6, 36.6)                  # the virtual screen (the front of the panel) is on screen, 0.5 s fades
# actions: (t0, t1, knob, kind, amount); "turn" amount = degrees clockwise seen from behind
KNOB_ACTIONS = [
    (24.4, 26.2, 0, "turn", -100),      # A anticlockwise: dimmer
    (26.6, 28.4, 0, "turn", +100),      # A clockwise: brighter again
    (29.0, 29.3, 0, "press", 0),        # A press: pause
    (30.8, 31.1, 0, "press", 0),        # A press: resume
    (31.6, 32.2, 1, "turn", +60),       # B clockwise: next artwork
    (33.0, 33.6, 1, "turn", -60),       # B anticlockwise: previous artwork
    (34.6, 34.9, 1, "press", 0),        # B press: like
]
KNOB_PRESS_MM = 1.2                     # how far a pressed knob travels
# what the panel does, as seen on the virtual screen (bake_leds.py, compose.py)
BRIGHTNESS = [(24.4, 1.0), (26.2, 0.22), (26.6, 0.22), (28.4, 1.0)]   # linear between the points, 1.0 outside
PAUSES = [(29.0, 30.8)]                 # the panel is off; the artwork resumes where it stopped
NEXT_ARTWORK = (31.9, 33.3)             # the "next" artwork is up between these (see PANEL)
HEART = (34.7, 36.2)                    # the like acknowledgement
VIRTUAL_LABELS = [                      # under the virtual screen: what the knob just did
    (24.4, 26.4, "dimmer"),
    (26.6, 28.6, "brighter"),
    (29.0, 30.8, "paused"),
    (30.8, 31.6, "playing"),
    (31.6, 33.0, "next artwork"),
    (33.0, 34.6, "previous artwork"),
    (34.6, 36.2, "liked"),
]

# the last stretch, the device from the front: the studio lights go down so the LEDs and the
# haze carry the picture (t0, t1, level): fade from t0 to t1 down to level, then hold
STUDIO_DIM = (38.6, 40.2, 0.12)

LAST_RENDER_FRAME = int(48.4 * FPS)     # after that the end card is composed without Blender

# --- what the panel shows (bake_leds.py): (t0, t1, source) --------------------------
# sources: "art:<file in docs/video/artworks>" (the user's selection, 2026-09-24; f12030f3 is
# the hero piece and leads), "gif:<file in firmware/tests/host/corpus/gifs>", "clock",
# "analogue", "weather", "stream"
PANEL = [
    (0.0,  7.0,  "art:f12030f3-d150-4af1-9b16-d3a65a4b1df8.webp"),   # the hero artwork
    (7.0,  31.9, "art:c5170051-2bad-48dd-b242-ad4c804d12f1.webp"),   # the sunset: from behind, the reassembly, the knob scene
    (31.9, 33.3, "art:05e2ecd8-8e10-4a1f-b828-19ce7a56771c.webp"),   # knob B "next": the bubbles
    (33.3, 38.6, "art:c5170051-2bad-48dd-b242-ad4c804d12f1.webp"),   # knob B "previous": the sunset again
    (38.6, 40.8, "art:d2afeeba-0b9e-4840-adbc-3c6bc2b47637.webp"),
    (40.8, 41.7, "clock"),
    (41.7, 42.6, "analogue"),
    (42.6, 43.6, "weather"),
    (43.6, 45.6, "stream"),
    (45.6, 48.4, "art:900f7d49-f1a4-4ba4-a54a-88e25de33e8d.webp"),
    (48.4, 50.0, "black"),
]

# --- captions (compose.py): (t0, t1, headline, line 2) ------------------------------
# v4: v2's seven short lines, the knob line moved into the knob scene and split per knob,
# the later beats shifted by 15 s. (v1 and v2, the 30 s sets, are in git history.)
CAPTIONS_V4 = [
    (0.8,  4.2,  "p64: a 64 × 64 pixel-art player", "128 mm · USB-C · open source"),
    (4.4,  8.4,  "The back: vents, six screws, one USB-C window, two knobs", ""),
    (8.8,  12.0, "Inside: the LED panel and the ESP32-S3 driver board", ""),
    (12.2, 15.2, "One 3D print, two USB-C adapters, six screws", ""),
    (16.4, 19.8, "Inside the shell: the seating ledge, six screw bosses, two encoder posts", ""),
    (20.6, 23.0, "No soldering for p64a", "p64b adds the two knobs"),
    (24.0, 30.8, "Knob A: brightness · press to pause", ""),
    (31.2, 36.4, "Knob B: next / previous · press to like", ""),
    (38.8, 43.4, "Art from microSD or Makapix Club · clock · weather", ""),
    (43.6, 47.4, "Live streams over Wi-Fi · web UI at p64.local", ""),
]
CAPTIONS = CAPTIONS_V4
CAPTION_FADE = 0.4

FADE_IN = (0.0, 0.8)                    # from black
FADE_OUT = (47.6, 48.4)                 # to black
END_CARD = (48.4, 50.0)                 # logo, repository, licence

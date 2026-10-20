# Intro animations

The boot animation (spec 6.4 and 15.1) is one of a suite of short reveals of the p64
logo, built on 2026-10-20 (p101) in place of the placeholder rings. The logo is embedded
as two layers, the panel with the bird (layer 0) and the wordmark drawn on top (layer 1),
and every animation brings the two in its own way: sliding past each other, dissolving in
turn, opening in bands that cross, bouncing, flipping, shattering. This file is the
catalogue; the code is `components/p64_intro/`, the design is in
[architecture.md](architecture.md) (component table) and the API in [api.md](api.md).

The suite has 26 animations so the owner can trim it to the 21 that will ship; the
ones to drop are not decided yet.

## How they run

- The window is `display.boot_animation_ms` (1 to 7 s, default 3 s; 0 = no boot
  animation). Every animation reaches the finished logo by 85 % of the window and holds
  it for the rest, so there is no separate hold setting; the first artwork may replace
  the logo as soon as the window ends (spec 15.1 unchanged).
- 50 fps (20 ms frames). A 64x64 render costs 0.6 to 7 ms on the device (the bench
  below), which leaves the 8 ms panel copy its room; 60 fps has shown rare late flips in
  Quality mode, so 50 was chosen (2026-10-20).
- `display.boot_animation` is `random` (the default: one of the suite at each boot,
  picked with `esp_random()` and named in the boot log and in the status document's
  `playback.boot.animation`) or an animation's name. The transparent parts of the layers
  show `display.background`.
- Each animation is a pure function of the window's normalised time and a per-boot seed
  (`intro::render`, `intro.hpp`); the shell that paces it is `IntroSource` in
  `main/show.cpp`, allocated in PSRAM with the 4 KB of working memory the Voronoi
  shatter needs. Layer pixels move by whole pixels (pixel art stays crisp); blends and
  tints are per pixel.
- The two layers are baked by `tools/gen_logo_assets.py` from
  `docs/images/logo/v2/p64-logo-v2-L0.png` and `-L1.png`: each is trimmed to its opaque
  box and keeps the box's canvas offset, so a layer drawn at its home lands exactly
  where the untrimmed image had it (layer 0 is 45x48 at (10, 5), layer 1 is 44x24 at
  (10, 36); 12.9 KB of RGBA in flash, no decoder at boot). The host test checks the
  baked layers against the PNGs through the firmware's PNG decoder.

## Seeing them

- **On the panel**: Settings > Display > Boot animation has the animation menu, **Play
  now** (the chosen one, or a random one) and **Play next** (steps through all 26 without
  changing the setting). Each plays for one slot of the configured length inside the
  Animation show, then the show swaps to a fresh artwork; nothing enters history. The
  API is `POST /api/v1/action/boot_animation {"name": ...}` and `GET
  /api/v1/boot_animations`.
- **In a browser**: `python tools\render_intro.py` (from `firmware/`, needs g++ and
  Pillow) compiles the same sources on the host and writes one GIF per animation plus an
  `index.html` that shows them all to `docs/images/intro/` (git-ignored: 26 GIFs are
  11 MB). Open `docs/images/intro/index.html`. Pillow quantises each frame to 256
  colours, so the wordmark's rainbow is close, not exact; the panel is the judge.
- **Timed**: `GET /api/v1/diag/intro_bench` renders every animation off-screen and
  reports min, average and maximum microseconds per frame; `tests/device/intro_smoke.py`
  holds every maximum under 10 ms and plays one (all with `--all`) on the panel with no
  late flips.

## The catalogue

In registry order (the index of `GET /api/v1/boot_animations`). "Panel" is layer 0,
"wordmark" layer 1. Render times are the device bench of 2026-10-20 after the rework below (average and
maximum per frame, microseconds, HTTP task on core 0 with Wi-Fi up; every one within the
10 ms the test holds).

| # | Name | Panel (layer 0) | Wordmark (layer 1) | Chance | Render avg / max |
|---|---|---|---|---|---|
| 1 | `fade_duet` | fades in over the first 60 % | fades in from 35 % to the end | none | 1040 / 2468 |
| 2 | `slide_cross` | slides down from above, easing out | slides up from below, overshoots and settles | none | 708 / 945 |
| 3 | `bounce_drop` | falls under gravity, two damped bounces | drops onto the panel, one bounce | none | 597 / 906 |
| 4 | `bands_cross` | vertical 4 px bands wipe down and up alternately, staggered | horizontal 4 px bands wipe right and left alternately | none | 1902 / 2387 |
| 5 | `dissolve_duet` | pixel dissolve by a per-pixel threshold | the same from a different draw, later | which pixel first | 1057 / 1707 |
| 6 | `iris_typewriter` | a circular iris opens from the centre | typed in column by column behind a blinking cursor | none | 690 / 1011 |
| 7 | `venetian_cross` | 4-row strips slide in from left and right alternately | 4-column strips slide in from top and bottom alternately | none | 2417 / 4006 |
| 8 | `assemble` | pixels fly in radially from outside, each with its own distance and delay | pixels streak in from beyond the right edge | distances, delays | 5220 / 6316 |
| 9 | `glitch_settle` | 4-row slabs jitter sideways with red/blue separation, stuttering, decaying | flickers on, jittering with it, then holds | the jitter | 2818 / 4960 |
| 10 | `channel_emerge` | red, green and blue ghosts slide in from three sides and recombine | a flat white silhouette that takes on its rainbow | none | 3010 / 4597 |
| 11 | `hue_spin` | fades in with hues turning twice round the wheel to lock | fades in with its rainbow sliding three turns to rest | none | 3300 / 4268 |
| 12 | `swirl_orbit` | pixels spiral in from far out, unwinding a turn | orbits the screen centre as one piece on a tightening circle | launch scatter, direction | 2218 / 3855 |
| 13 | `blinds_flip` | eight slats open edge-on to flat, shaded, top first | opens as one slat | none | 1055 / 1850 |
| 14 | `scanline_cross` | a bright scan bar sweeps down revealing it | a bar sweeps up from the bottom revealing it | none | 557 / 815 |
| 15 | `stamp` | fades up quietly | comes down from 4x size like a stamp, white flash on impact | none | 1765 / 5134 |
| 16 | `shatter_letters` | sixteen Voronoi shards fly in from outside | the p from the left, the 6 from above, the 4 from the right | the shards | 3844 / 5160 |
| 17 | `curtain_banner` | curtains part from the middle, bright seams | unrolls down over it with a bright roll edge | none | 688 / 945 |
| 18 | `wave_settle` | fades in with rows displaced by a damping sine wave | fades in with columns bobbing on a wave that settles later | none | 913 / 1217 |
| 19 | `plasma_duet` | emerges in soft plasma blobs that grow and join | the same through a finer, differently phased field | the phases | 1782 / 2311 |
| 20 | `starburst_sparkle` | pixels fly in radially, overshoot and snap back | ignites pixel by pixel, a white spark each, cooling to colour | distances, order | 5315 / 8267 |
| 21 | `tiles_diagonal` | 4x4 tiles pop in, a flat bright block for a blink first | wiped in along the diagonal with a lit edge | tile order | 676 / 1015 |
| 22 | `flip_card` | turns about its vertical axis from edge-on, shaded | turns about its horizontal axis, after | none | 1642 / 2748 |
| 23 | `spotlight_sweep` | a spotlight roams over it in the dark, then widens | a beam of light sweeps along it, letters flaring white | none | 2535 / 4126 |
| 24 | `crt_on` | a bright line grows across, the picture expands out of it | a vertical line grows tall, the wordmark opens sideways | none | 1017 / 2140 |
| 25 | `fountain` | rises from below and settles | pixels tossed up from the bottom on parabolas, landing in place | launch order, arcs | 1300 / 1794 |
| 26 | `tune_in` | television snow in its own shape, locking in pixel by pixel | fades up in greys, saturating into its rainbow | the snow, the order | 1813 / 3124 |

The first bench of 2026-10-20 found three animations over the 10 ms line: `hue_spin`
(a round trip through HSV per pixel), `swirl_orbit` (a square root, an arctangent and
four trigonometric calls per pixel) and `plasma_duet` (three sines per pixel, with the
phases hashed per pixel too), with `wave_settle` close behind (a sine per pixel). They
were reworked the same day: the hue turn is a 3x3 matrix built once a frame, the spiral
is one rotation and one radial scale per frame scattered per pixel by the hash, the
plasma's three waves are per-row, per-column and per-diagonal tables, the wave's
displacement is one value per row or column: 3.3, 2.2, 1.8 and 0.9 ms average after
(the table), from 8.3, 12.1, 12.0 and 4.7 before.

## Adding one

1. Write `components/p64_intro/src/ia_<name>.cpp` with a `render(Frame &, const
   Context &, float u)` and `extern const Anim k<Name>{"<name>", render};` (the
   `extern` gives the namespace-scope constant external linkage for the registry).
   `u` runs over (0, 1), the window up to the hold: converge on the finished logo by
   0.95, so the handoff to the held frame is invisible (the host test checks the last
   frame before the hold against it, pixel for pixel). Use the helpers of
   `intro_common.hpp`: the layers `L0()` and `L1()`, `draw`, `draw_alpha`, `draw_tinted`,
   `draw_resampled`, `draw_if`, `each`, the hash `chance(seed, a, b)` for anything random
   (never a clock or a global generator), `window`, the easings, `mix`, `hue_turn`.
2. Declare it in `registry.cpp` and add it to `kAnims`, and add the file to the
   component's `CMakeLists.txt`.
3. `python tests\host\run.py --tc "intro*"` (the count in `intro.cpp` goes up by one),
   `python tools\render_intro.py <name>` to look at it, then on the device
   `tests\device\intro_smoke.py` for the bench, and this catalogue.

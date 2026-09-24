# The p64b concept video

A 30-second rendered concept video of p64b (the variant with the two rotary encoders),
built from the v7b enclosure model, the mock-ups in the same OpenSCAD source and the
firmware's own assets: the panel plays four animations from the host-test GIF corpus, the
clock and weather faces are drawn with the firmware's bundled pixel fonts and weather
icons, and a plasma stands in for a live stream. Made on 2026-09-24 for the public: what
p64 is, what is inside it, and what it does. The device on screen is the model, not a
photo: nothing of v7 has been printed yet (see `enclosure/README.md`).

The video itself (`docs/p64b-concept-v2.mp4`, 1920 x 1080, 30 fps, H.264, silent) is
not committed, to keep the repository small; this folder holds everything that makes it,
so one command regenerates it. `build/` is git-ignored.

Two caption versions exist over the same render: **v1**, the first cut (twelve captions of
1.5 to 3 s, two lines each; `docs/p64b-concept-v1.mp4`, `compose.py --captions v1`), and
**v2**, after the user found v1's text too much and too brief (seven captions of 3 to
4.6 s, one line each; the default). Both caption sets are in `storyboard.py`. **v3**
(`docs/p64b-concept-v3.mp4`) is a new render with v2's captions: the explosion flies out
horizontally (the tilted axis had sent parts under the table) with outer parts leading
(a shaft no longer pokes through its knob), the user's artworks, the clock fixed.

## Regenerate

Needs OpenSCAD 2021.01 (`C:\Program Files\OpenSCAD`), Blender 5.2 (`C:\Program Files\
Blender Foundation\Blender 5.2`; Cycles on the GPU, OptiX or CUDA, the CPU works but is
slow), ffmpeg on the PATH, and a Python with Pillow and numpy (the system one). From
`docs/video/` in PowerShell 7:

```
.\make.ps1            # everything below, in order; 45 min to 2 h on an RTX 5050 laptop (3 s a frame
                      # when the GPU boosts, 10 s under the laptop's 14 W software power cap)
.\make.ps1 -Quick     # 16 samples and every fourth frame, for a look at the motion (7.5 fps)
```

The steps, if you want one of them alone:

| Step | Command | What it does |
|---|---|---|
| 1 | `openscad -o build/values.echo -D piece="values" pieces.scad` and one `-o build/<piece>.stl -D piece="<piece>"` per piece | `pieces.scad` includes the enclosure source with `encoders = true` and exports each mock-up (panel frame, LED board, controller, adapters, cradle insert, encoder boards, knobs, screws) as its own STL in the shell's design coordinates, plus the numbers Blender needs. |
| 2 | `python bake_leds.py` | One 64 x 64 PNG per video frame in `build/leds/` (what the panel shows, from `storyboard.PANEL`), and the LED aperture mask. |
| 3 | `blender -b -P scene.py -- [--start N --end N] [--samples N] [--frames a,b,c]` | Builds the scene (the committed `enclosure/output/p64b/v7b/p64_enclosure_print.stl` un-printed back into design coordinates, the pieces, the LED face with the baked sequence, a dark studio, the camera and the explosion from `storyboard.py`), saves `build/p64b.blend`, renders `build/frames/`. |
| 4 | `python compose.py [--captions v1\|v2] [--frames a,b,c]` | Bloom around the LEDs, fades, captions, the end card; encodes `../p64b-concept-v2.mp4` with ffmpeg (about 8 min for the 900 frames). |

`storyboard.py` is the one place with every time: the camera beats, the explosion, what
the panel shows, the captions. Change it and rerun from step 2 (the panel) or 3 (camera,
explosion) or 4 (captions only: a captions change needs no re-render).

## What is in the 30 seconds

| Time | Picture | Caption (v2) |
|---|---|---|
| 0 to 4 s | Front three-quarter view, an animation playing | what p64 is, size |
| 4 to 8 s | Orbit to the back: the vents, the six screws, the USB-C window, the two knobs; the explosion begins | the two knobs and their roles |
| 8 to 15 s | Exploded view from the side, panel to screws: panel, controller, adapters, insert, encoder boards, shell, nuts and knobs, screws | the panel and the driver board; the print, the adapters, the screws |
| 15 to 19 s | Everything comes back together while the camera returns to the front | no soldering for p64a, p64b adds the knobs |
| 19 to 28 s | Feature beats on the panel: a second animation, the digital and analogue clocks, the weather, a live-stream plasma, a fourth animation | card and Makapix Club, clock, weather; streams and the web UI |
| 28 to 30 s | End card | logo, repository, licence |

## Honest notes

- The mock-ups are the enclosure source's own (`ghost_*` modules): boxes and cylinders in
  the right places and sizes, not the real boards' shapes. The screws are modelled here
  (`pc_screws` in `pieces.scad`), M3 x 10 pan heads sitting 1 mm below the back face.
- The LED face is a 64 x 64 texture behind a grid of rounded apertures on a glossy black
  body, a look, not the panel's optics; the real GOB panel diffuses more.
- The artworks (since v3) are the user's selection in `docs/video/artworks/` (animated
  64 x 64 WebP; `f12030f3...` is the hero piece and leads; the sunset, the Van Gogh and the
  living room follow), read with their own frame durations from the ANMF chunks. v1 and
  v2 used corpus GIFs from `firmware/tests/host/corpus/gifs/`; `storyboard.PANEL` takes
  either (`art:` or `gif:`). The LED face's emission is `P64_LED_STRENGTH` (default 4.5 under
  the Khronos PBR Neutral view transform, `P64_VIEW`, whose highlights roll off; v1 and v2
  used Standard at 9, which clipped pale artworks to white; AgX was tried and desaturated
  the LEDs). A thin scattering volume around the device (`P64_HAZE`, default 0.03 per
  metre, 0 removes it) lets the LEDs light the air and the floor in front of them (the
  user's request of 2026-09-24: brighter LEDs with a little volumetric light).
- The analogue clock ticks once per second of video time since v3 (v1 and v2 ran it six
  times too fast).
- Nothing here is measured. The video is a concept render of the v7b model as designed.

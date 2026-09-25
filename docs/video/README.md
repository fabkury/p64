# The p64b concept video

A 50-second rendered concept video of p64b (the variant with the two rotary encoders),
built from the v7b enclosure model, the mock-ups in the same OpenSCAD source and the
firmware's own assets: the panel plays four animations from the host-test GIF corpus, the
clock and weather faces are drawn with the firmware's bundled pixel fonts and weather
icons, and a plasma stands in for a live stream. Made on 2026-09-24 for the public: what
p64 is, what is inside it, and what it does. The device on screen is the model, not a
photo: nothing of v7 has been printed yet (see `enclosure/README.md`).

The video itself (`docs/p64b-concept-v4.mp4`, 1920 x 1080, 30 fps, H.264, silent) is
not committed, to keep the repository small; this folder holds everything that makes it,
so one command regenerates it. `build/` is git-ignored.

Versions so far, all kept side by side in `docs/` (git-ignored): **v1**, the first cut
(twelve captions of 1.5 to 3 s, two lines each); **v2**, the same render after the user
found v1's text too much and too brief (seven captions of 3 to 4.6 s, one line each);
**v3** (`p64b-concept-v3-draft*.mp4`, 15 fps drafts), a new render: the explosion flies out
horizontally (the tilted axis had sent parts under the table) with outer parts leading
(a shaft no longer pokes through its knob), the user's artworks, the clock fixed,
brighter LEDs under PBR Neutral, the haze; **v4** (the current video, rendered at 96 samples on 2026-09-24) adds the knob
scene, 45 s in all: after the reassembly the camera settles behind the device, which sits
in the left half of the frame, and a "virtual screen" (the front of the panel, drawn flat
in the right half by `compose.py` from the same baked LED frames) shows what each knob
does as it turns and is pressed: A dims and brightens, A pressed blanks the panel (pause) and resumes, B
steps to the next and the previous artwork, B pressed likes (a heart). The knobs are
separate pieces on pivots with a pointer line each (`pieces.scad`), and a faint point
light marks the active knob. The 30 s timelines of v1 to v3 are in git history (commit
d682551).

## Regenerate

Needs OpenSCAD 2021.01 (`C:\Program Files\OpenSCAD`), Blender 5.2 (`C:\Program Files\
Blender Foundation\Blender 5.2`; Cycles on the GPU, OptiX or CUDA, the CPU works but is
slow), ffmpeg on the PATH, and a Python with Pillow and numpy (the system one). From
`docs/video/` in PowerShell 7:

```
.\make.ps1            # everything below, in order; the v4 pass (96 samples, the haze and the glow light)
                      # takes several hours on an RTX 5050 laptop, depending on the GPU's power state
.\make.ps1 -Quick     # 16 samples and every fourth frame, for a look at the motion (7.5 fps)
```

The steps, if you want one of them alone:

| Step | Command | What it does |
|---|---|---|
| 1 | `openscad -o build/values.echo -D piece="values" pieces.scad` and one `-o build/<piece>.stl -D piece="<piece>"` per piece | `pieces.scad` includes the enclosure source with `encoders = true` and exports each mock-up (panel frame, LED board, controller, adapters, cradle insert, encoder boards, knobs, screws) as its own STL in the shell's design coordinates, plus the numbers Blender needs. |
| 2 | `python bake_leds.py` | One 64 x 64 PNG per video frame in `build/leds/` (what the panel shows, from `storyboard.PANEL`), and the LED aperture mask. |
| 3 | `blender -b -P scene.py -- [--start N --end N] [--step N] [--samples N] [--frames a,b,c]` | Builds the scene (the committed `enclosure/output/p64b/v7b/p64_enclosure_print.stl` un-printed back into design coordinates, the pieces, the LED face with the baked sequence, a dark studio, the camera and the explosion from `storyboard.py`), saves `build/p64b.blend`, renders `build/frames/`. |
| 4 | `python compose.py [--frames a,b,c]` | Bloom around the LEDs, the virtual screen of the knob scene, fades, captions, the end card; encodes `../p64b-concept-v4.mp4` with ffmpeg (about 13 min for the 1500 frames). ||| 4 | `python compose.py [--frames a,b,c]` | Bloom around the LEDs, the virtual screen of the knob scene, fades, captions, the end card; encodes `../p64b-concept-v4.mp4` with ffmpeg (about 13 min for the 1500 frames). |

`storyboard.py` is the one place with every time: the camera beats, the explosion, what
the panel shows, the captions. Change it and rerun from step 2 (the panel) or 3 (camera,
explosion) or 4 (captions only: a captions change needs no re-render).

## What is in the 50 seconds (v4)

| Time | Picture | Caption |
|---|---|---|
| 0 to 4 s | Front three-quarter view, the hero artwork playing | what p64 is, size |
| 4 to 8 s | Orbit to the back: the vents, the six screws, the USB-C window, the two knobs; the explosion begins | the back of the shell |
| 8 to 15 s | Exploded view from the side, panel to screws: panel, controller, adapters, insert, encoder boards, shell, nuts and knobs, screws | the panel and the driver board; the print, the adapters, the screws |
| 15 to 20 s | The camera swings to the front-right and above and looks into the shell's cavity (the seating ledge, the six bosses, the encoder posts, the vents from inside), a light in there fading in and out, then flies back | inside the shell |
| 20 to 23 s | Everything comes back together while the camera moves behind the device | no soldering for p64a, p64b adds the knobs |
| 23 to 37 s | The knob scene: the device in the left half, the virtual screen in the right; knob A turns down and up, is pressed twice; knob B turns forward and back, is pressed | knob A: brightness, press to pause; knob B: next / previous, press to like |
| 37 to 39 s | The virtual screen fades, the camera returns to the front | |
| 39 to 48 s | Feature beats on the panel: Van Gogh, the digital and analogue clocks, the weather, a live-stream plasma, the living room | card and Makapix Club, clock, weather; streams and the web UI |
| 48 to 50 s | End card | logo, repository, licence |

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
  the LEDs). A scattering volume around the device (`P64_HAZE`, default 0.10 per
  metre, 0 removes it) lets the LEDs light the air in front of them (the user's request
  of 2026-09-24: brighter LEDs with volumetric light). The studio lights are unlinked
  from the volume (Cycles light linking), otherwise their scatter greys the scene and
  swamps the panel; and since the emissive LED face alone scatters too little to read,
  an area light on the face, coloured and dimmed per frame from the baked picture's mean
  (`build/led_mean.json`, written by `bake_leds.py`) and linked to the volume only, does
  the visible glow (`P64_GLOW_W`, default 1.2 W at a full-white panel; 4 W washes the
  picture out, 40 W whites the frame).
- The analogue clock ticks once per second of video time since v3 (v1 and v2 ran it six
  times too fast).
- v4's other look decisions, all the user's: the LED face and mask are matte (the glossy
  coat reflected the key light as a hot spot), the studio lights dim to 12 % for the last,
  frontal stretch (`storyboard.STUDIO_DIM`), a paused panel is off, the stream plasma
  runs at 55 %, the active knob is marked by a floating emissive ring that exists only
  while lit, and the exploded view gets a look into the shell's cavity with its own light.
- Nothing here is measured. The video is a concept render of the v7b model as designed.

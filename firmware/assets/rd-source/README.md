# Retro Diffusion sources of two clock faces (p075, p076, 2026-10-03)

The raw outputs of the Retro Diffusion API (<https://www.retrodiffusion.ai>) that the
faces `horizon_rd` and `aquarium` are drawn from, and the record of what was paid.

- `calls.json`: every paid call in order, with its exact payload (the input image left
  out), its price and its output files. **$2.18 was spent** in two rounds: $1.85 of a $5
  budget for the candidates (p075), then $0.33 of a further $1 for a larger sun and a
  smaller moon (p076). Account balance $7.16 before, $4.98 after.
- `used/`: the outputs the assets are cut from. `fish_*_k20.png`, `fish_*_k16.png` and
  `moon_k12.png` are the service's free `k_centroid_downscale` of the fish animations
  (laid out as strips) and of `moon_0.png` (not in `calls.json`, they cost nothing).
- `unused/`: what was paid for and not used, kept so nobody pays for it twice.

`tools/prep_rd_clock_assets.py` turns `used/` into `assets/clock-png/` offline and for
free; `tools/gen_clock_assets.py` embeds those PNG files in the firmware;
`tools/mock_clock_faces.py` draws the faces. How to call the API (client, prices,
pitfalls) is the user's `retrodiffusion-api` repository; the key never enters this one.

| Face | Used | Price |
|---|---|---|
| Horizon-RD | `land_1` (rd_plus__low_res, two candidates) | $0.12 |
| | `moon_0` (rd_fast__low_res, 14 px, shrunk to 12 for free), `cloud_0`, `cloud_1` | $0.09 |
| | `sun3_1` (rd_fast__low_res, three candidates, a 12 px disc) | $0.09 |
| Aquarium | `tank_0` (rd_plus__low_res, two candidates) | $0.12 |
| | `tank_subtle_0` (subtle_motion, 16 frames; it loops: seam 2 % against steps of 1 to 3 %) | $0.25 |
| | three goldfish stills (rd_fast__low_res) | $0.12 |
| | three goldfish animations (advanced idle, 8 frames each; played forth and back, they do not loop) | $0.42 |
| Not used | `sun_0` (spiky), `sun2_0` and `sun2_1` (8 px, too small beside the moon), `fish_c_0` (a black moor, too dark for the panel) | $0.12 |
| | `sun4_0`, `sun4_1` (rd_plus, with rays: brown rims), `moon2_0`, `moon2_1` (rd_plus: the shrunk first moon kept the look the user had seen) | $0.24 |
| | `land_night`, `tank_night` (image_edit): the edit redrew both scenes instead of relighting them, so night is computed from the day picture | $0.36 |
| | `land_subtle_0` (subtle_motion on the landscape): came back almost still (under 1 % of pixels change), so the lake's glints are computed | $0.25 |

### The analogue candidates (p077, 2026-10-03)

A further $0.90 (of a $5 budget for that task; $3.08 in all), all `rd_plus__low_res`,
three candidates a call:

| Call | Result | Price |
|---|---|---|
| `brass` at 64x64 | good plates, but the enamel only 46 px across (not used; `unused/brass_*.png`) | $0.18 |
| `station` and `station2` at 64x64 | blank dials 46 px across in a wide margin, one with painted hands (not used) | $0.36 |
| `station3` at 80x80 | `station3_1` used: the middle 64x64 has a 59 px dial | $0.18 |
| `brass3` at 80x80 | `brass3_0` used: the middle 64x64 has a 56 px enamel dial and the plate's corners | $0.18 |

What the service taught here, beyond the notes in `retrodiffusion-api`:

- `image_edit` does not keep a 64x64 composition pixel-aligned, whatever the prompt says;
  do not plan a day/night cross-fade on it.
- Asking for "a flat solid white sky" gives a sky that keys out with a flood fill; the
  model still painted a sun in it.
- `rd_advanced_animation__idle` animates a fish on a flat colour well (the flat colour
  comes back exact and keys out); 32 px is the smallest input, and the free k-centroid
  downscale of the whole strip to 20 px cells keeps the frames consistent.
- A round subject asked to "fill the picture" at 64x64 still comes with a margin; generating
  at 80x80 and keeping the middle 64x64 gives a dial that fills the panel, at the same pixel size.
- "No numerals, no hands" holds in about two pictures of three; ask for three.
- `rd_fast__low_res` goes down to 16x16; how much of the canvas the subject fills follows the
  prompt ("fills almost the whole picture" gave a 12 px disc, without it 8 px).

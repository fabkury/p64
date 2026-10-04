# Retro Diffusion sources of the clock face candidates (p075, 2026-10-03)

The raw outputs of the Retro Diffusion API (<https://www.retrodiffusion.ai>) that the two
candidate faces Horizon-RD and aquarium are drawn from, and the record of what was paid.

- `calls.json`: every paid call in order, with its exact payload (the input image left
  out), its price and its output files. The user's budget was $5; **$1.85 was spent**
  (account balance $7.16 before, $5.31 after).
- `used/`: the outputs the assets are cut from. `fish_*_k20.png` and `fish_*_k16.png` are
  the service's free `k_centroid_downscale` of the three 32 px fish animations laid out as
  strips (not in `calls.json`, they cost nothing).
- `unused/`: what was paid for and not used, kept so nobody pays for it twice.

`tools/prep_rd_clock_assets.py` turns `used/` into `assets/clock-candidates/` offline and
for free; `tools/mock_rd_clock_faces.py` draws the faces. How to call the API (client,
prices, pitfalls) is the user's `retrodiffusion-api` repository; the key never enters this
one.

| Face | Used | Price |
|---|---|---|
| Horizon-RD | `land_1` (rd_plus__low_res, two candidates) | $0.12 |
| | `moon_0`, `sun2_0`, `cloud_0`, `cloud_1` (rd_fast__low_res, background removed) | $0.15 |
| Aquarium | `tank_0` (rd_plus__low_res, two candidates) | $0.12 |
| | `tank_subtle_0` (subtle_motion, 16 frames; it loops: seam 2 % against steps of 1 to 3 %) | $0.25 |
| | three goldfish stills (rd_fast__low_res) | $0.12 |
| | three goldfish animations (advanced idle, 8 frames each; played forth and back, they do not loop) | $0.42 |
| Not used | `sun_0` (spiky), `fish_c_0` (a black moor, too dark for the panel) | $0.06 |
| | `land_night`, `tank_night` (image_edit): the edit redrew both scenes instead of relighting them, so night is computed from the day picture | $0.36 |
| | `land_subtle_0` (subtle_motion on the landscape): came back almost still (under 1 % of pixels change), so the lake's glints are computed | $0.25 |

What the service taught here, beyond the notes in `retrodiffusion-api`:

- `image_edit` does not keep a 64x64 composition pixel-aligned, whatever the prompt says;
  do not plan a day/night cross-fade on it.
- Asking for "a flat solid white sky" gives a sky that keys out with a flood fill; the
  model still painted a sun in it.
- `rd_advanced_animation__idle` animates a fish on a flat colour well (the flat colour
  comes back exact and keys out); 32 px is the smallest input, and the free k-centroid
  downscale of the whole strip to 20 px cells keeps the frames consistent.
- `rd_fast__low_res` goes down to 16x16 and fills the canvas: a 14 px moon, an 8 px sun.

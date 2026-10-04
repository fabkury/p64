# Clock face candidates (prompts p043 to p046, 2026-09-26)

The six faces were designed here as mock-ups, approved by the user on 2026-09-26 and then
implemented in the firmware (`components/p64_widgets/src/face_*.cpp`, settings value
`clock.face`). `tools/mock_clock_faces.py` stays the design reference: it draws the assets
(`assets/clock/<face>/`, baked by `tools/gen_clock_assets.py`), these review images and the
test references under `tests/host/corpus/clock/` that `tests/host/run.py` compares with the
firmware's pixels. Edit a face in the mock, regenerate, then match it in the C++ until the
comparison is exact again.

Every mock-up is made the way the firmware makes it: bitmap assets (the PNGs under
`firmware/assets/clock/<face>/`, drawn by the script and editable by hand afterwards)
stamped onto a 64x64 frame, plus text from the bundled pixel fonts through the firmware's
own glyph tables, integer-scaled, without anti-aliasing. `<face>.png` is the real 64x64 frame, `<face>@8x.png` the same enlarged
nearest-neighbour for looking at, `contact-sheet.png` all six side by side.

| Face | Idea | Assets | Text | Moves |
|---|---|---|---|---|
| `flip` | A split-flap clock: two graphite tiles with rounded corners, a top highlight, a hinge line through the numerals and axle pins; ivory numerals; the weekday and date small and dim above; an amber seconds rail below (30 ticks, one per two seconds). | `tile.png` (30x34, stamped twice), `digits.png` (ten 9x16 Helvetica-bold-style numerals) | Capital Hill 6 px for the date | The rail every second (optional); the flip on the minute (below) |
| `nixie` | Four Nixie tubes on a brass-trimmed walnut base: domed glass with a reflection, warm haze inside, wire numerals in orange with a one-pixel halo, socket and pins, a neon colon; the date in dim amber under the base. | `tube.png` (13x38, stamped four times), `digits.png` (ten 7x11 wire numerals), `base.png` (64x4) | Capital Hill 6 px for the date | The colon blinks (optional) |
| `horizon` | A landscape computed from where the sun really is (below). The time on top in outlined white, the date on the ground. | `sun.png`, `cloud.png`, `far-hills.png`, `near-hills.png`, `tree.png` (masks tinted per moment); the moon is drawn procedurally from its phase | Capital Hill 6 px at 2x for the time, 1x for the date | Every minute |
| `words` | A word clock: a 12x9 grid of letters behind a brushed-steel bezel, the letters that spell the time lit in warm white and the rest barely there ("IT IS HALF PAST TEN AM"); four corner dots add the minutes past the five. The grid is p64's own layout, not a product's. | `alphabet.png` (a 3x5 capital alphabet drawn for the grid), `bezel.png` (64x64 frame with the brush, the inner shadow and four screw heads) | none from the bundled fonts | Every five minutes, and a dot a minute |
| `hourglass` | The hour as sand: the top bulb holds what is left of it, the bottom the heap that has run, a stream between them that loses a grain every second; the surface of the top sand dips into a funnel and the heap rises to a peak under the neck. The hour and the minute stacked beside it in the flip's numerals, the date under them. | `frame.png` (26x52: walnut end plates, brass posts, the glass outline), `sand.png` (4x4 two-tone dither tile); the flip's `digits.png` shared | Everyday Slight 5 px for the date | The stream every second, the sand every minute |
| `orrery` | A brass orrery on a star chart: the Earth (with its Moon on an arm) goes round the Sun once in twelve hours, the Moon round the Earth once an hour, Mercury round the Sun once a minute; dotted brass rings for the orbits, twelve ticks at the rim, the time in figures on a brass plaque at the foot. | `plate.png` (64x64: chart, rings, ticks, plaque, drawn once), `sun.png` (9x9), `earth.png` (5x5), `moon.png` and `mercury.png` (3x3) | Capital Hill 6 px on the plaque | Mercury every second, the Moon and the Earth every minute |

The digits of the Everyday fonts carry a dotted zero and High Birth is the only bold plain
one, so the flip and nixie faces draw their own numerals as assets; that is also what those
clocks look like in life (Helvetica cards, wire cathodes).

## The second three

`words-moments.png`, `hourglass-moments.png` and `orrery-moments.png` show each at six
times of day; `hourglass@8x.gif` and `orrery@8x.gif` a few seconds of each. All three are
deliberately unlike the first three and each other: one is typographic and shows no
figures at all, one shows time as a quantity, one as a mechanism. The words face is
twelve-hour by nature (AM and PM are on the grid); the other two take 12/24 h.

## The flip's animation

`flip-minute.gif` (10:32 to 10:33) and `flip-hour.gif` (10:59 to 11:00), with `@8x`
enlargements. The firmware would keep one 30x34 sprite per tile (the tile asset with its two
numerals stamped on, rebuilt when the pair changes) and animate a change in ten frames of
45 ms: the old upper leaf falls, its rows squashed towards the hinge and darkened as it
tilts away from the light (heights 15, 12, 8, 4, 1), then the new lower leaf lands, growing
from the hinge and lit brighter while it faces the light (heights 2, 6, 10, 14, 16). Each
frame is three row-range copies of the two sprites (the top of the new card, the squashed
leaf, the bottom of the old card), so it costs nothing beyond the copies; the hour tile
starts one frame after the minute tile, as on a real clock. The GIFs hold the last seconds
before the minute, the flip, and the rest.

## The horizon's model

The scene is procedural, from the sun's position rather than from the hour, so every
hour of every day, and every place, looks like itself:

- Sun elevation and azimuth from the latitude and longitude the weather widget already
  holds, the day of the year and the local time (NOAA's low-precision formulas, well under
  a degree; pure arithmetic for a host-tested `solar.cpp`). Checked: New York on
  2026-09-26 rises at 06:50 and sets at 18:50 in the model, as in the almanac.
- The viewer faces the equator: the sun rises on the left and sets on the right in the
  northern hemisphere, mirrored in the southern; the east-west component is projected on
  the view plane so a sun near the zenith stays in the middle. Its height on the panel is
  its elevation.
- The sky is keyed by the elevation (nine stops from -18 degrees, astronomical night, to
  the zenith), with the twilight glow on the sun's side of the horizon only. The sun is a
  red disc within 6 degrees of the horizon and gold with rays above.
- Stars come out from 4 degrees below and are full from 12 below. The moon shows its
  phase (terminator drawn per pixel, from its elongation from the sun) where it really
  is: the Astronomical Almanac's low-precision moon, about 0.3 degrees (since
  2026-10-01; a phase-only estimate before, up to 20 degrees off); on a dark sky its
  night side shows faintly. The sun and the moon rise and set on the far hills' line
  (since 2026-10-01; before, the far hills hid the setting sun from 18 degrees up): the
  disc rests on the line at +0.7 degrees and has slid behind it by -0.83, the almanac's
  sunset (under a pixel a minute); the day's arc is the old round one lifted onto a
  raised horizon (the higher end of the hill line) up to row 18 at the zenith, the local
  line bending only the first 5 degrees; where the hills stand higher than that horizon
  (the peak), they hide a low body.
- The weather widget's current condition, when a location is set, adds clouds (one, two,
  four or seven by the cover, lit by the sky), greys the sky, and streaks rain or drops
  snow, with a snow line on the ground.
- Clouds drift with the minute; the hills are hazed by the horizon colour by day and black
  by night.

`horizon-day.png` shows twelve moments of one day in New York; `horizon-variants.png` the
same afternoon hour in four weathers, and dusk in Sao Paulo (sun setting on the left, the
southern hemisphere), midwinter noon in Tromso (the sun never up, a crescent moon),
midsummer near midnight in Reykjavik (the sun just under the horizon at the north) and noon
in Singapore (the sun near the zenith). Without a location the face would use a fixed
mid-latitude and no weather, so it still runs.

Settings each would honour when implemented: 12/24 h (AM/PM as a small mark), seconds
(rail, colon blink), date order, and the accent colour where a face has one (the flip's
rail, the nixie's glow); the face colour settings of the digital face do not apply to the
themed faces.

## The LED face (prompts p052 and p053, 2026-09-28)

A seven-segment clock drawn from VEXED's **Digital Display** (`assets/fonts/digital-display`,
CC BY 4.0), a 15x19 pixel font that only looks right at multiples of 19 px: the digits and
the colon are rasterised once from the TTF at 19 px into `assets/clock/led/digits.png` and
`colon.png` (`gen_fonts.py` does not bundle this font, it is too tall for the overlay).
HH:MM in one row would be 67 px, so the hours sit at the upper left and the minutes at the
lower right, a staircase, with the seconds at the lower left in two 5x9 seven-segment
digits of the same style (`mini.png`) and the indicators AM, PM and ALM at the upper right
(the words face's 3x5 alphabet). Every digit position keeps its unlit "8" as a faint ghost,
as on the real displays; the leading zero of the hour is blank in 12 h; the ALM dot is lit
for decoration only (an alarm has no meaning yet).

Mocked up as two faces (LED and VFD) and approved on 2026-09-28 with two changes: the VFD
became a style of the one face, and the highlight that swept along the top of the frame
every eight seconds was dropped as artificial; after the first look at the panel (p054)
the ghost and the glow of every style were cut to a third, because the matrix lifts the
dark end and the unlit segments competed with the lit ones; that overshot (too faint to
notice, p055) and half the first values is where they settled (the review images show the
final values). The `led_style` setting picks one of five:

| Style | Look | Assets |
|---|---|---|
| `red`, `green`, `amber`, `blue` | An LED bedside clock: the segments behind a filter tinted in the colour, a dark plastic bezel with the light on its top and left edges and a recessed inner edge, the labels printed on the filter with a lit square dot beside each (AM or PM in 12 h). | `led/bezel.png` (64x64 with the window clear), `digits.png`, `colon.png`, `mini.png` |
| `vfd` | A vacuum fluorescent display: cyan-green phosphor (150, 255, 225) on dark glass with a faint mesh (alternate rows), in a chrome frame with a bright top-left edge; the indicator words themselves light up, a bell beside ALM; a six-bar meter dances in the band between the rows, purely for life. | `led/vfd-frame.png` and the same digits |

What moves: every digit change (the seconds every second, the minutes every minute)
cross-fades over five frames of 40 ms, the segments that go out falling from lit to ghost
while the ones that come in rise, the shared ones staying lit, the way a display with a
slow response changes; the colon blinks with the blinking-colon setting; a one-pixel glow
around every lit pixel breathes over four seconds (255 down to 140 of its colour and back),
so the face redraws every 200 ms whenever it is up (decided 2026-09-28); the VFD's meter
bars ride two unrelated triangle waves each. The face's clock is the local time of day in
milliseconds, so every frame is a function of the moment: the firmware (`face_led.cpp`)
draws the same integer arithmetic over a 0..255 intensity map and a ghost map, and the
seventeen `led-*` references (the styles, 12 h, the blinking colon, frames of the fades)
are pixel-exact.

`led.png` is the 64x64 frame (10:32:37, 24 h, seconds on), `led@8x.png` enlarged,
`led-vfd.png` and `led-vfd@8x.png` the VFD style; `led-styles.png` the five styles;
`led-moments.png` six times of day in 12 h; `led.gif` and `led-vfd.gif` (and `@8x`) eight
seconds from 10:31:57 across the minute change, frame by frame as the firmware draws them.

## Two faces from Retro Diffusion art (p075 and p076, 2026-10-03)

Designed here as mock-ups (p075), approved and implemented the same day (p076): the
settings values `horizon_rd` and `aquarium`, `face_horizon_rd.cpp` and
`face_aquarium.cpp`. Both are drawn from pixel art generated with the Retro Diffusion
API; the raw pictures, every call's payload and price are in `assets/rd-source/`
($2.18 spent), `tools/prep_rd_clock_assets.py` cuts them into `assets/clock-png/<face>/`,
and those PNG files are embedded in the firmware as they are and decoded when the face
starts (`picture.hpp`), unlike the other faces' baked sprites. Decisions taken with the
user: the horizon's computed sky over painted land; a goldfish tank with decor, the time
small on a sign (the time only, no AM or PM), the light following day and night; PNG
assets for both; a 12 px sun and a 12 px moon (the first mock had 8 and 14).

| Face | Idea | Assets | Text | Moves |
|---|---|---|---|---|
| `horizon_rd` | The horizon face's sky and almanac (colours by the sun's elevation, glow on the sun's side, stars, the sun and the moon where they are, the moon's phase, the weather's clouds, rain and snow) over a painted landscape: snow peaks, a lake, pines and a log cabin. The land keeps its colours by day, sinks to a blue night and is warmed at the twilights; the lake mirrors the sky and glitters under the sun and the moon; the cabin's two windows are lit while it is dark. | `land.png` (64x38, at row 26), `lake.png` (the water's mask), `lights.png` (the windows), `sun.png` (12x12), `moon.png` (12x12, the phase's terminator is computed over it), `cloud-a.png`, `cloud-b.png` | Everyday Vast Black 11 px for the time, Capital Hill for AM/PM beside it, Everyday Slight 5 px for the date at the bottom right, on the lake, clear of the cabin; one outline at 75 % black | The lake's glints every 500 ms, the rest every minute |
| `aquarium` | A goldfish tank: a stone castle, a plant, an open treasure chest, gravel, and a wooden sign that carries the time. Three goldfish swim from wall to wall and turn, each on its lane, wagging its tail: a small yellow one and an orange one behind the decor, a large red and white one in front of everything but the sign. By night (from the sun's elevation) the water goes dark blue, the castle's window and door glow, the yellow fish hides and the others swim at half speed. | `tank.png` (16 frames of 64x64: the plant sways, bubbles rise from the chest), `front.png` (the same frames, only what stands in front of the far fish), `sign.png` (27x10), `lights.png`, `fish-a.png` (20x17), `fish-b.png` (26x19), `fish-c.png` (14x10), eight frames each | Everyday Standard 6 px on the sign, cream with a carved shadow | Every 150 ms |

`horizon_rd-day.png` is one day in New York (the same twelve moments as `horizon-day.png`),
`horizon_rd-variants.png` the weather, 12 h, the full and the half moon and Sao Paulo,
`horizon_rd@8x.gif` six seconds of the lake; `aquarium-moments.png` six moments,
`aquarium.gif` and `aquarium-night.gif` (with `@8x`) fourteen and ten seconds of the tank.
The aquarium is integer arithmetic after the sun's elevation, so its references are
compared pixel for pixel; Horizon-RD's within the horizon's tolerance.

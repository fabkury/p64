# Clock face candidates (prompt p043, 2026-09-26)

Mock-ups of new clock widgets for review, drawn by `tools/mock_clock_faces.py`. Nothing here
is implemented in the firmware yet: a face is built only once its design is approved.

Every mock-up is made the way the firmware would make it: bitmap assets (the PNGs under
`assets/<face>/`, drawn by the script and editable by hand afterwards) stamped onto a 64x64
frame, plus text from the bundled pixel fonts at their native size, integer-scaled, without
anti-aliasing. `<face>.png` is the real 64x64 frame, `<face>@8x.png` the same enlarged
nearest-neighbour for looking at, `contact-sheet.png` the three side by side.

| Face | Idea | Assets | Text | Moves |
|---|---|---|---|---|
| `flip` | A split-flap clock: two graphite tiles with rounded corners, a top highlight, a hinge line through the numerals and axle pins; ivory numerals; the weekday and date small and dim above; an amber seconds rail below (30 ticks, one per two seconds). | `tile.png` (30x34, stamped twice), `digits.png` (ten 9x16 Helvetica-bold-style numerals) | Capital Hill 6 px for the date | The rail every second (optional); the leaves could animate a flip on the minute later |
| `nixie` | Four Nixie tubes on a brass-trimmed walnut base: domed glass with a reflection, warm haze inside, wire numerals in orange with a one-pixel halo, socket and pins, a neon colon; the date in dim amber under the base. | `tube.png` (13x38, stamped four times), `digits.png` (ten 7x11 wire numerals), `base.png` (64x4) | Capital Hill 6 px for the date | The colon blinks (optional) |
| `horizon` | A landscape that follows the time of day: the sky's gradient, the sun by day and the moon by night on one arc, stars that fade with daylight, two clouds drifting with the minute, hills and a tree that darken at night; the time on top in outlined white, the date on the ground. `horizon-day.png` shows eight moments. | `sun.png`, `moon.png`, `cloud.png`, `far-hills.png`, `near-hills.png`, `tree.png` (masks tinted per time) | Capital Hill 6 px at 2x for the time, 1x for the date | The sky every minute; the sun and moon every few minutes |

The digits of the Everyday fonts carry a dotted zero and High Birth is the only bold plain
one, so the flip and nixie faces draw their own numerals as assets; that is also what those
clocks look like in life (Helvetica cards, wire cathodes).

Settings each would honour when implemented: 12/24 h (AM/PM as a small mark), seconds
(rail, colon blink), date order, and the accent colour where a face has one (the flip's
rail, the nixie's glow); the face colour settings of the digital face do not apply to the
themed faces.

# Encoder development record

Historical purchases and bench results, September–October 2026. These entries retain
their original dates and assumptions. For the current build, use the
[two-encoder guide](encoders-soldered.md) and [project status](../status.md).

## 1. Parts

On hand (2026-09-21): the two 5880 boards, the two 5528 knobs, the 4209 and 4397 cables
(150 mm), the 4399 cable (50 mm), the 2.2 k resistors, the ELEGOO breadboards, fine
tweezers, jumper wires or header pins (the 5880 bags include a short header strip each).
The soldering kit of the table below arrived on 2026-09-26, the multimeter (Klein MM325)
and the solder wick on 2026-09-27, the replacement 5880 for knob B on 2026-10-01; the
200 mm 4401 cable and a 2 mm hex key are on hand too (2026-10-01). The
"no multimeter" notes in sections 3.3, 4 and 6.1 stay for a builder without one: the
probe firmware (section 5) is the check that matters.

Still needed:

| Part | For | Note |
|---|---|---|
| Electrical tape or 3 mm heat-shrink | wrapping the cable joints | tape is fine |
| Tier 2 only: 1/4 W 2.2 k resistors (optional) | neater on the board than 1/2 W | the 1/2 W parts work, they are just bulky |
| Tier 2 splice only: wire strippers, 2 mm heat-shrink, a lighter or heat gun | joining the cables | only if the pin-to-socket joints are not wanted |

The soldering kit (on hand since 2026-09-26). The picks are examples chosen on 2026-09-23 for this
project's three small jobs (a surface-mount bridge, two through-hole resistors, perhaps a
rework); any part meeting the spec does:

| Part | Spec | Example | Note |
|---|---|---|---|
| Soldering iron | temperature-controlled, fine conical or small chisel tip | Pinecil V2 (ships with the TS-B2 short tip, fine enough for A0) | runs on any USB-C PD charger of 65 W or more (12 to 20 V) with a C-to-C cable rated 3 A or more; the Miniware TS101 is the equal, dearer alternative |
| Iron stand | holds the hot iron between joints | any "TS100 / Pinecil" stand | the Pinecil comes without one |
| Solder | leaded 63/37, rosin flux core, 0.8 mm (0.031") | Kester 44, 1 oz dispense pak (24-6337-0027) | eutectic: no pasty range, the most forgiving on 0.3 mm pads; 1 oz is a lifetime of this project; avoid unbranded "60/40" spools |
| Flux pen | rosin or no-clean | Kester 951 or MG Chemicals 8341 | makes the A0 bridge a one-touch job, and pulls an A0/A1 bridge apart |
| Tip cleaner | brass wool in a holder | Hakko 599B-02 | does not cool the tip as a wet sponge does |
| Solder wick | desoldering braid, 2 mm | Chemtronics Soder-Wick #2 or MG Chemicals | removes surplus solder (section 8) |
| Flush side cutters | small, flush-cutting | Hakko CHP-170 | trims the resistor leads (section 6.1) |
| Isopropyl alcohol | 91 to 99 %, with cotton swabs | any | cleans the flux off |
| Safety glasses | any | | clipped leads fly |

Not needed: a fume extractor (a window or a fan is enough for five joints), a
helping-hands clip (tape holds the board), spare tips, a silicone mat.

### 5.1 Bench state and what to resume (2026-09-26)

- Knob A: accepted on the bench. Stage B: 10 detents clockwise counted +10 and 10 back
  landed on exactly 0, two presses and a long press registered, no read errors, clockwise
  positive (so `encoders_invert` stays off). Stage C: see the progress log for what was
  verified on the device.
- The knob cap (2026-09-27): with the cap pushed fully down the shaft, three clicks
  registered nothing (`presses` stayed 0, no read errors, the switch pin steady at
  released); with the cap off, three clicks counted 3 and toggled the pause. The shaft's
  push travel is well under a millimetre and a cap seated on the nut takes it up. Seat
  the cap with a visible gap, about 1 mm, above the nut, and check `presses` on
  `GET /api/v1/diag/encoders` after fitting it (section 8). Reseated that way the same
  day, the click pauses and resumes the show with the cap on.
- Board B: silent after the A0 bridge. Its ON LED lights; alone on the 4397, on either of
  its sockets, it answers at no address (`?scan=1` empty) with both bus lines idle high;
  board A put back on the same plug answers at once. The photo of the back shows a small
  blob on the A0 pads, A1 and A2 untouched, the 103 pull-up network and the NeoPixel pads
  clean.
- 2026-09-27, with the meter: the bridge conducts, no short to A1 or A2 (the top pad of
  each jumper is a shared rail, so A0-to-top-A1 beeps by design), the pull-ups read
  19.93 k, the board alone powers up (3Vo 3.3 V, SDA and SCL 2.88 V) and still answers at
  no address. So the bridge is not visibly at fault and the chip is not running or not
  talking; the board has no RST pad to read. The bridge was then wicked off (A0 open on
  the meter, pads intact) and the board plugged alone on the 4397 again: ON LED lit,
  3Vo 3.3 V, three scans empty, both lines idle high. **Verdict, 2026-09-27: board B's
  microcontroller is dead; replace the board** (a fresh 5880; ask Adafruit or the
  reseller about the one that never answered). The dead board keeps its knob, nut and
  washer for the replacement. Knob A runs alone meanwhile.
- 2026-10-01, the replacement 5880, tested before soldering: alone on the 4397 it lit
  green and answered at 0x36 (hardware id 0x55, 47 detents, 0 read errors). A0 bridged
  with flux and a small bead; on the meter the A0 pads beep, the chip-side pads of A0 and
  A1 do not, the A1 pads do not, SDA to SCL reads about 20 k. Alone again: blue, 0x37.
- 2026-10-01, both knobs, the bench acceptance of section 5, all passed on the breadboard
  harness with the 50 mm 4399 between the boards: A green and B blue at boot;
  `encoders_smoke.py --expect 2 --watch 45` 0 failures with both knobs turned at once
  (dozens of detents each way, presses on both, 0 read errors on either board); knob B
  turns next and previous and its press likes the Makapix artwork on the panel (the log
  says "liked post 1567"; on a card artwork it says there is nothing to like); the 4399
  pulled from board B while running: knob B dropped after ten failed reads, knob A kept
  the brightness with 0 errors and was never lost, and B was found again at 0x37 without
  a reboot, with its blue identify, about 15 s after the plug went back.
- Next: tier 2 (section 6): the two pull-ups soldered onto board A, and the cables mated
  pin to socket, not spliced (decided 2026-10-01). Then the shell (section 7): the v7b
  print is ordered and awaited (2026-10-01); when it arrives, lay the washer and nut in a
  spot-face before anything else (their diameter is still unmeasured).


## Original bench measurements

| Item | Expected | Measured |
|---|---|---|
| 3V3 / GND pin colours on the 4209 | red / black (keyed plug, 2026-09-26) | |
| Rows 5 and 10, resistors only | ~2.7 V | |
| Rows 5 and 10, both boards | ~2.86 V | |
| A0 continuity on board 2 | beep | beep (2026-09-27, Klein MM325) |
| A0 to the A1 pads on board 2 | no beep | top A1 pad beeps, bottom does not; top A2 to top A1 beeps, bottom to bottom does not: the top row is the jumpers' shared rail, the chip-side pads are clear (2026-09-27) |
| A1 pads on board 2 | no beep | no beep |
| SDA to SCL on board 2, unpowered | ~20 k | 19.93 k (the two 10 k pull-ups in series); SDA or SCL to the VIN pad reads OL, as an unpowered regulator does |
| SDA, SCL, VIN to GND on board 2 | no beep | no beep |
| Board 2 alone on the 4397, powered | 3Vo 3.3 V, SDA and SCL ~2.86 V | 3Vo 3.3 V, SDA 2.88 V, SCL 2.88 V; `?scan=1` empty, both lines idle at 1 |
| `flash.ps1` with IO46 pulled high | works | works with the chain attached (2026-09-26): the chain stays connected for good |
| Free internal heap, largest block, before the probe | from `/diag/memory` | |

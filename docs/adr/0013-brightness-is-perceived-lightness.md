---
status: accepted
date: 2026-09-27
---

# The brightness number is perceived lightness, dimmed in software below the driver's floor

The user's brightness 1 to 255 (the setting, the knob, the API, the night schedule's
target, Makapix commands) is an even scale of perceived lightness: 255 is the panel's
full light, 128 looks about half as bright (about a fifth of the light), and 1 is a
night-light glow, a sixteenth of the light the driver's own floor allowed. The display
turns the number into a share of the full light through a pure curve (lightness even in
the number, light its cube, `p64_display/light_curve.hpp`) and hands that share to the
vendored driver's light plan (`Hub75Driver::set_light`, a p64 patch): the plan picks the
output-enable level at or above the driver's floor whose light reaches the share and
scales the LUT's targets for the rest, so every one of the 255 values is a distinct
level, and below the floor the LUT scale alone dims, at one bit of tonal depth per
halving. The knob's brightness rule steps a fixed 7 per detent, an even step to the eye:
37 detents from darkest to full. Stored values are not migrated; their numbers keep, their
light changes.

## Context

Until now the number went to the driver's quadratic curve, which is floored at 17 on a
64-wide panel (four pixel clocks on the top plane, so the binary ratios of the planes
hold), and the output-enable windows are whole clocks: the panel had 59 distinct levels,
the lowest at 6.4 % of the full light (to the eye about a third of full, a lamp in a dark
bedroom, not a night light), and values 1 to 7 were the same picture. Spec 3.2 accepted
that for v1 and ruled software dimming out. The brightness knob of p64b (stage C,
2026-09-26) stepped 10 % of the value per detent and so walked seven detents through that
dead zone. A review of the driver's tonal headroom (p050, 2026-09-27) found that dimming
by shorter output-enable windows collapses the depth in the same proportion as scaling
the LUT would, and only a driver chip's current-gain register would dim without that
cost. The panel's chips turned out to be FM6124HJ (read on the back of the panel on
2026-09-27, after this decision): a plain shift register with no registers at all, so
that lever does not exist on this hardware and software dimming is the only route.

## Decision

- The brightness number means lightness. L(v) = L1 + (v - 1) / 254 * (1 - L1) and the
  light is L^3, with L1 the cube root of the floor share, 127 / 1979 / 16 (about 0.4 % of
  full). The choice of a sixteenth: eight grey levels per channel remain at brightness 1,
  which pixel art survives; an eighth would keep sixteen levels but stay clearly a lit
  panel in a dark room, a thirty-second would be nearly binary.
- The driver gets `set_light(light_q16)` beside `set_brightness()`. The plan
  (`p64bcm::plan_light`, pure, host-tested) never chooses a level below the driver's
  floor, so the plane ratios hold at every value; above the floor the LUT scale stays
  between about 0.75 and 1 and every request lands within one clock of light.
- The knob rule is a fixed step of 7 (`inputs::kDetentStep`), 37 detents each way.
- No migration of stored values: the default 255 is unchanged, only the development
  device exists, and the meaning is recorded here and in spec 3.2.
- The plan in force is visible in the status document (`panel.light`, `panel.oe_level`,
  `panel.lut_scale`) and checked on the device by `tests/device/brightness_smoke.py`.

## Consequences

- Every value 1 to 255 is a distinct picture; the knob and the slider act on every step.
- A saved brightness looks dimmer than before (128: half the light before, a fifth now).
- The bottom of the scale is posterized by design (eight levels at 1, sixteen at about 8,
  the full 127 codes of the floor from about 40 up); that is the trade for the darkness,
  and the eye's own contrast sensitivity drops with it.
- The Makapix contract's "brightness 1 to 255" keeps its range; a command's number now
  means lightness, as the setting does.
- Spec 3.2 is amended; the driver's curve (`set_brightness`) stays in the vendored code
  for the hardware tests and upstream parity but the product firmware no longer calls it.

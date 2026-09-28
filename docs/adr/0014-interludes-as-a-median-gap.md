---
status: accepted
date: 2026-09-28
---

# Interludes are entered as a median gap in minutes, the per-swap chance follows

The user sets, per widget, how many minutes typically pass between its interludes: the
median gap M, an integer of 5 to 1440, or 0 for never (defaults: clock 30, weather 180,
temperature 0). The firmware derives the per-swap probability from the auto-swap
interval T in seconds, p = 1 - 2^(-T / 60M), so that the gap between two interludes of
that kind has median M whatever the interval. An interval longer than the gap is
unsatisfiable (a gap can never be shorter than one interval): that kind is off, and the
status document and the settings page say so. When two kinds win the same swap the one
with the larger M shows (ties in the fixed order clock, weather, temperature), and the
kinds that lose slots that way are rolled a little above their target so that their
realised per-swap probability is exactly p again. `POST /api/v1/action/interlude` plays
a widget as an interlude now, for one slot.

## Context

Spec 6.1 gave each widget a per-swap probability of 0 to 100 %, rolled in a fixed order
at every auto-swap. The number was honest but meant nothing to the person setting it: a
"1 %" clock is a clock about every hour on 30 s swaps and about every two days on 1 h
swaps, and changing the auto-swap interval silently changed how often every interlude
came. The user (p056, 2026-09-28) asked for the setting people actually think in, "the
clock every half hour or so", with the firmware doing the arithmetic against its own
interval, and for the rarer interlude to win a coincidence.

The gap between interludes of one kind, counted in swaps, is geometric: with probability
p per swap, the chance of no interlude in n swaps is (1 - p)^n. Its median is the n at
which that chance is a half, which gives p directly from the wanted median. The median,
not the mean, was chosen because it is what a person notices ("usually about every half
hour") and because the geometric distribution's mean is pulled by its tail.

## Decision

- The setting is `widgets.interlude_minutes {clock, weather, temperature}`: 0 = never,
  else 5 to 1440 (a day); values below 5 clamp to 5, above 1440 to 1440. Defaults 30,
  180 and 0. The old `interlude_percent` key is dropped without a migration (nothing had
  shipped).
- The plan (`rules::interlude_plan`, `main/show_rules.cpp`, pure and host-tested): for
  each kind with M > 0 and T <= 60M, p = 1 - 2^(-T / 60M); T > 60M or T = 0 (no
  auto-swap) is off, with the reason kept for the status. The kinds are rolled in
  priority order, the largest M first (ties: clock, weather, temperature), the first to
  win takes the slot; a lower kind's rolled probability is p divided by the chance that
  no higher kind won, capped at 1, so its realised per-swap probability is p.
- The roll compares a uniform 32-bit number against the rolled probability scaled to
  2^32 (the old roll had one percent of resolution; a weather every three hours on 30 s
  swaps is 0.19 % per swap).
- The status document's `playback.interludes` carries, per kind, `median_minutes`,
  `per_swap_percent` (the realised p, to a tenth) and `state` (`rolled`, `never`,
  `no_auto_swap`, `interval_longer`); the settings page shows that under each field.
- `POST /api/v1/action/interlude {"widget": ...}` plays the widget as an interlude now
  (one slot, into history), 409 outside the Animation show; the settings page has a
  button per widget and the device smoke test uses it, since no setting can force an
  interlude any more.

## Consequences

- Changing the auto-swap interval keeps every interlude's cadence in minutes; the
  per-swap chances move instead.
- The maths was checked offline first (`firmware/tools/interlude_sim.py`: realised rates
  within 1.5 sigma of the target over three million swaps for every corner, the naive
  uncompensated roll 87 sigma slow for a pre-empted kind) and is replayed on the host
  with a seeded generator (`tests/host/unit/show.cpp`).
- With three kinds at the same gap and an interval equal to it, the third gets nothing
  (the first two already take every slot) while the status still reports its target
  chance; that is the unsatisfiable corner, not worth a special case.
- The discrete median sits between M/T and M/T + 1 swaps, so the realised median gap is
  M to M plus one interval; at an interval equal to the gap the "median" is two
  intervals. Acceptable, and stated in the code.

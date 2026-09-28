#!/usr/bin/env python3
r"""Offline check of the interlude cadence engine (p056, 2026-09-28; ADR 0014).

    python tools\interlude_sim.py [--swaps N] [--seed S]

The user enters, per widget, the median gap M_i in minutes between interludes of that
kind (0 = never, else 5..1440). At every auto-swap (interval T seconds) each kind is
rolled with a per-swap probability; the maths, mirrored by `interlude_plan()` in
main/show_rules.cpp:

- Gaps in swaps are geometric, so the gap has median M_i when the chance of no win in
  M_i/T swaps is a half: p_i = 1 - 2^(-T / (60 M_i)). T > 60 M_i is unsatisfiable (the
  gap can never be shorter than one interval): the kind is off.
- Kinds are rolled in priority order, the largest M_i first (ties: clock, weather,
  temperature); the first to win takes the slot. That is "each rolled independently,
  the larger M_i wins a coincidence".
- A lower-priority kind loses the slots a higher one takes, so its rolled probability is
  q_i = p_i / (chance that no higher kind won), which makes its realised per-swap
  probability exactly p_i again (capped at 1 when the higher kinds leave nothing).

This script derives the probabilities, simulates the swaps and reports the median gap
of every kind in minutes against M_i, for the defaults and the awkward corners.
"""

import argparse
import random
import statistics

NAMES = ("clock", "weather", "temperature")


def plan(minutes, interval_s, compensate=True):
    """Per-kind rolled probability and the roll order, from the medians and the interval."""
    q = [0.0, 0.0, 0.0]
    order = sorted((i for i in range(3) if minutes[i] > 0), key=lambda i: (-minutes[i], i))
    if interval_s == 0:
        return q, order
    surviving = 1.0  # chance that no higher-priority kind has won this swap
    for i in order:
        m_s = 60 * minutes[i]
        if interval_s > m_s:
            continue  # unsatisfiable: off
        p = 1.0 - 2.0 ** (-interval_s / m_s)
        if compensate:
            q[i] = 1.0 if surviving <= 0.0 else min(1.0, p / surviving)
        else:
            q[i] = p
        surviving *= 1.0 - q[i]
    return q, order


def roll(q, order, rng):
    for i in order:
        if q[i] > 0.0 and rng.random() < q[i]:
            return i
    return -1


def simulate(minutes, interval_s, swaps, rng, compensate=True):
    q, order = plan(minutes, interval_s, compensate)
    last = [None, None, None]
    gaps = [[], [], []]
    for n in range(swaps):
        w = roll(q, order, rng)
        if w < 0:
            continue
        if last[w] is not None:
            gaps[w].append(n - last[w])
        last[w] = n
    return q, gaps


def report(title, minutes, interval_s, swaps, rng, compensate=True):
    """Two checks per kind: the realised per-swap rate is p_i (binomial 4 sigma; the
    sharp test of the compensation), and the sample median gap is M_i within one
    interval (the discrete median sits between M_i/T and M_i/T + 1 swaps) plus the
    median's own sampling error (about the mean gap over the square root of the count)."""
    q, gaps = simulate(minutes, interval_s, swaps, rng, compensate)
    print("%s: T = %d s, M = %s%s" % (title, interval_s, minutes, "" if compensate else " (naive)"))
    ok = True
    for i in range(3):
        target = minutes[i]
        if target == 0 or interval_s == 0 or interval_s > 60 * target:
            good = not gaps[i]
            print("  %-11s off%s" % (NAMES[i], "" if good else "  <-- WRONG, shown %d times" % len(gaps[i])))
        else:
            p = 1.0 - 2.0 ** (-interval_s / (60.0 * target))
            n = len(gaps[i]) + 1
            rate = n / swaps
            sigma = (p * (1 - p) / swaps) ** 0.5
            rate_ok = abs(rate - p) <= 4 * sigma
            med = statistics.median(gaps[i]) * interval_s / 60.0 if gaps[i] else float("nan")
            mean = statistics.mean(gaps[i]) * interval_s / 60.0 if gaps[i] else float("nan")
            tol = interval_s / 60.0 + 3 * mean / max(1, len(gaps[i])) ** 0.5
            med_ok = bool(gaps[i]) and abs(med - target) <= tol
            good = rate_ok and med_ok
            print("  %-11s q = %8.5f  rate %8.5f (p %8.5f, %+.1f sigma)  median %8.2f min (target %d +-%.2f, %d shown)%s" %
                  (NAMES[i], q[i], rate, p, (rate - p) / sigma, med, target, tol, n, "" if good else "  <-- OFF"))
        ok = ok and good
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--swaps", type=int, default=2_000_000)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    checks = [
        ("defaults", (30, 180, 0), 30),
        ("an hour of clock on 30 s swaps", (60, 0, 0), 30),
        ("two kinds close together", (5, 5, 0), 60),
        ("three kinds at the floor", (5, 5, 5), 60),
        ("non-integer swaps per gap", (5, 0, 0), 45),
        ("the interval equals the gap", (5, 0, 0), 300),
        ("the interval is longer than the gap", (5, 180, 0), 600),
        ("no auto-swap", (30, 180, 0), 0),
        ("all off", (0, 0, 0), 30),
        ("a day", (1440, 30, 5), 30),
    ]
    ok = True
    for title, minutes, interval in checks:
        ok = report(title, minutes, interval, a.swaps, rng) and ok
    print()
    print("Without compensation the pre-empted kind drifts (not a check):")
    report("two kinds close together", (5, 5, 0), 60, a.swaps, rng, compensate=False)
    print()
    print("ALL GOOD" if ok else "SOME CHECK FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
r"""Generates components/p64_net/src/tz_table.inc: IANA zone name -> POSIX TZ rule.

    python tools\gen_tz_table.py [year]

For every zone in Python's zoneinfo database the script takes the reference year
(default: the current one), reads the UTC offsets in force and, when daylight saving
applies, finds the two transitions and expresses them as POSIX "Mm.w.d/time" rules
(nth weekday of the month, 5 = last). Zones with more or fewer than two transitions in
the year, or with rules that are not "nth weekday", get a fixed-offset rule for their
January offset, and are listed at the top of the generated file so they can be checked.

Needs the tzdata package on Windows (pip install tzdata).
"""

import datetime as dt
import os
import re
import sys
import zoneinfo

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "components", "p64_net", "src", "tz_table.inc")


def posix_offset(seconds):
    """UTC offset as a POSIX TZ offset string (sign inverted: west of UTC is positive)."""
    total = -seconds
    sign = "-" if total < 0 else ""
    total = abs(total)
    h, rem = divmod(total, 3600)
    m, s = divmod(rem, 60)
    if s:
        return f"{sign}{h}:{m:02d}:{s:02d}"
    if m:
        return f"{sign}{h}:{m:02d}"
    return f"{sign}{h}"


def abbrev(name, offset_seconds):
    """A POSIX-legal abbreviation: alphabetic names as-is, others as <+HHMM>."""
    if re.fullmatch(r"[A-Za-z]{3,6}", name or ""):
        return name
    total = offset_seconds
    sign = "+" if total >= 0 else "-"
    total = abs(total)
    h, rem = divmod(total, 3600)
    m = rem // 60
    return f"<{sign}{h:02d}{m:02d}>" if m else f"<{sign}{h:02d}>"


def transitions(zone, year):
    """Instants (UTC) at which the UTC offset changes during `year`, with the local
    wall time before the change (hour resolution refined to the minute)."""
    tz = zoneinfo.ZoneInfo(zone)
    found = []
    t = dt.datetime(year, 1, 1, tzinfo=dt.timezone.utc)
    end = dt.datetime(year + 1, 1, 1, tzinfo=dt.timezone.utc)
    prev = t.astimezone(tz).utcoffset()
    step = dt.timedelta(hours=1)
    while t < end:
        n = t + step
        off = n.astimezone(tz).utcoffset()
        if off != prev:
            # refine to the minute
            lo, hi = t, n
            while (hi - lo) > dt.timedelta(minutes=1):
                mid = lo + (hi - lo) / 2
                if mid.astimezone(tz).utcoffset() == prev:
                    lo = mid
                else:
                    hi = mid
            found.append((hi, prev, off))
            prev = off
        t = n
    return found


def rule_for(instant_utc, offset_before):
    """Mm.w.d/time for a transition, in the local time in force before it."""
    local = instant_utc + offset_before
    month, day, weekday = local.month, local.day, (local.weekday() + 1) % 7  # POSIX: Sunday = 0
    week = (day - 1) // 7 + 1
    # If no later same-weekday exists in the month, it is the last one: week 5.
    days_in_month = (dt.date(local.year + (month == 12), (month % 12) + 1, 1) - dt.date(local.year, month, 1)).days
    if day + 7 > days_in_month:
        week = 5
    secs = local.hour * 3600 + local.minute * 60 + local.second
    time = ""
    if secs != 2 * 3600:
        h, rem = divmod(secs, 3600)
        m = rem // 60
        time = f"/{h}" if m == 0 else f"/{h}:{m:02d}"
    return f"M{month}.{week}.{weekday}{time}"


def posix_for(zone, year):
    tz = zoneinfo.ZoneInfo(zone)
    jan = dt.datetime(year, 1, 15, 12, tzinfo=tz)
    jul = dt.datetime(year, 7, 15, 12, tzinfo=tz)
    jan_off, jul_off = int(jan.utcoffset().total_seconds()), int(jul.utcoffset().total_seconds())
    trans = transitions(zone, year)
    if jan_off == jul_off and not trans:
        return f"{abbrev(jan.tzname(), jan_off)}{posix_offset(jan_off)}", True
    if len(trans) != 2:
        return f"{abbrev(jan.tzname(), jan_off)}{posix_offset(jan_off)}", False
    # Standard time = the smaller offset (POSIX DST is std + 1h by default; explicit
    # when it differs). Southern hemisphere: DST in force in January.
    (t1, before1, after1), (t2, before2, after2) = trans
    std_off = min(jan_off, jul_off)
    dst_off = max(jan_off, jul_off)
    std_name = jan.tzname() if jan_off == std_off else jul.tzname()
    dst_name = jul.tzname() if jul_off == dst_off else jan.tzname()
    # The transition into DST is the one whose offset increases.
    into_dst = t1 if after1 > before1 else t2
    out_dst = t2 if into_dst is t1 else t1
    into_before = before1 if into_dst is t1 else before2
    out_before = before2 if out_dst is t2 else before1
    dst_part = abbrev(dst_name, dst_off)
    if dst_off - std_off != 3600:
        dst_part += posix_offset(dst_off)
    rule = f"{abbrev(std_name, std_off)}{posix_offset(std_off)}{dst_part},{rule_for(into_dst, into_before)},{rule_for(out_dst, out_before)}"
    return rule, True


def main():
    year = int(sys.argv[1]) if len(sys.argv) > 1 else dt.date.today().year
    zones = sorted(z for z in zoneinfo.available_timezones() if "/" in z or z == "UTC")
    rows = []
    irregular = []
    for z in zones:
        if z.startswith("Etc/") or z.startswith("SystemV/") or z.startswith("posix/") or z.startswith("right/"):
            continue
        try:
            rule, regular = posix_for(z, year)
        except Exception as e:  # noqa: BLE001
            irregular.append(f"{z}: {e}")
            continue
        if not regular:
            irregular.append(f"{z}: fixed to its January offset ({rule})")
        rows.append((z, rule))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        f.write(f"// Generated by tools/gen_tz_table.py for {year} from Python's zoneinfo; do not edit.\n")
        f.write(f"// {len(rows)} zones. Zones without a regular two-transition rule use a fixed offset:\n")
        for line in irregular:
            f.write(f"//   {line}\n")
        f.write("// {name, posix}\n")
        for z, rule in rows:
            f.write(f'{{"{z}", "{rule}"}},\n')
    print(f"{len(rows)} zones written to {os.path.relpath(OUT)}; {len(irregular)} without a regular rule")


if __name__ == "__main__":
    main()

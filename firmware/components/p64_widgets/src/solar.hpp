// Where the sun and the moon are, for the horizon face (spec 7.1): NOAA's low-precision
// solar position (well under a degree), the moon's phase from the synodic month, and a
// rough lunar position (the moon trails the sun by its phase along the ecliptic, the
// orbit's tilt ignored). Pure arithmetic, host-tested against the almanac; the same
// formulas as tools/mock_clock_faces.py.
#pragma once

namespace p64::widgets::solar {

struct Position {
  float elevation;  // degrees above the horizon (negative below)
  float azimuth;    // degrees from north, clockwise, 0..360
};

// `year_day` is tm_yday (0 = 1 January); `hour_local` the local time in hours (13.5 =
// 13:30); `tz_hours` the local zone's offset from UTC, DST included (-4 for New York in
// September).
Position sun(float latitude, float longitude, float tz_hours, int year_day, float hour_local);
// 0 = new, 0.25 = first quarter, 0.5 = full, 0.75 = last quarter.
float moon_phase(int year, int year_day, float hour_local);
Position moon(float latitude, float longitude, float tz_hours, int year_day, float hour_local, float phase);

}  // namespace p64::widgets::solar

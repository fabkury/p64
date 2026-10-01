#include "solar.hpp"

#include <cmath>

namespace p64::widgets::solar {
namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double deg) { return deg * (kPi / 180.0); }
double degrees(double rad) { return rad * (180.0 / kPi); }

Position elevation_azimuth(double lat, double decl, double ha) {
  double sin_el = std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(ha);
  if (sin_el > 1) sin_el = 1;
  if (sin_el < -1) sin_el = -1;
  const double el = std::asin(sin_el);
  double az = std::atan2(std::sin(ha), std::cos(ha) * std::sin(lat) - std::tan(decl) * std::cos(lat)) + kPi;
  az = std::fmod(degrees(az), 360.0);
  if (az < 0) az += 360.0;
  return {static_cast<float>(degrees(el)), static_cast<float>(az)};
}

double fractional_year(int year_day, double hour_local, double tz_hours) {
  return 2 * kPi / 365 * (year_day + (hour_local - tz_hours - 12) / 24);
}

double equation_of_time(double g) {
  return 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) -
                   0.040849 * std::sin(2 * g));
}

double declination(double g) {
  return 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) +
         0.000907 * std::sin(2 * g) - 0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);
}

}  // namespace

Position sun(float latitude, float longitude, float tz_hours, int year_day, float hour_local) {
  const double g = fractional_year(year_day, hour_local, tz_hours);
  const double tst = hour_local * 60.0 + equation_of_time(g) + 4.0 * longitude - 60.0 * tz_hours;
  const double ha = radians(tst / 4 - 180);
  return elevation_azimuth(radians(latitude), declination(g), ha);
}

namespace {

// Days from J2000.0 (2000-01-01 12:00 UT) to a local moment.
double j2000_days(int year, int year_day, double hour_local, double tz_hours) {
  const auto leaps = [](int y) { return (y - 1) / 4 - (y - 1) / 100 + (y - 1) / 400; };
  return 365.0 * (year - 2000) + (leaps(year) - leaps(2000)) + year_day + (hour_local - tz_hours) / 24 - 0.5;
}

// The sun's apparent ecliptic longitude, degrees (the Astronomical Almanac's low-precision formula).
double sun_longitude(double n) {
  const double g = radians(357.528 + 0.9856003 * n);
  return 280.460 + 0.9856474 * n + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g);
}

struct Ecliptic {
  double longitude, latitude, parallax;  // degrees
};

// The moon's geocentric ecliptic position and horizontal parallax: the Astronomical
// Almanac's low-precision series (about 0.3 degrees), the eccentric orbit and its tilt.
Ecliptic moon_ecliptic(double n) {
  const double t = n / 36525;
  const auto s = [t](double a, double b) { return std::sin(radians(a + b * t)); };
  const auto c = [t](double a, double b) { return std::cos(radians(a + b * t)); };
  return {218.32 + 481267.881 * t + 6.29 * s(135.0, 477198.87) - 1.27 * s(259.3, -413335.36) +
              0.66 * s(235.7, 890534.22) + 0.21 * s(269.9, 954397.74) - 0.19 * s(357.5, 35999.05) -
              0.11 * s(186.5, 966404.03),
          5.13 * s(93.3, 483202.02) + 0.28 * s(228.2, 960400.89) - 0.28 * s(318.3, 6003.15) -
              0.17 * s(217.6, -407332.21),
          0.9508 + 0.0518 * c(135.0, 477198.87) + 0.0095 * c(259.3, -413335.36) + 0.0078 * c(235.7, 890534.22) +
              0.0028 * c(269.9, 954397.74)};
}

}  // namespace

float moon_phase(int year, int year_day, float hour_local, float tz_hours) {
  const double n = j2000_days(year, year_day, hour_local, tz_hours);
  double phase = std::fmod((moon_ecliptic(n).longitude - sun_longitude(n)) / 360.0, 1.0);
  if (phase < 0) phase += 1.0;
  return static_cast<float>(phase);
}

Position moon(float latitude, float longitude, float tz_hours, int year, int year_day, float hour_local) {
  const double n = j2000_days(year, year_day, hour_local, tz_hours);
  const Ecliptic e = moon_ecliptic(n);
  const double obliquity = radians(23.439 - 0.0000004 * n);
  const double lon = radians(e.longitude), lat = radians(e.latitude);
  const double ra =
      std::atan2(std::sin(lon) * std::cos(obliquity) - std::tan(lat) * std::sin(obliquity), std::cos(lon));
  const double decl =
      std::asin(std::sin(lat) * std::cos(obliquity) + std::cos(lat) * std::sin(obliquity) * std::sin(lon));
  const double sidereal = radians(280.46061837 + 360.98564736629 * n + longitude);
  Position p = elevation_azimuth(radians(latitude), decl, sidereal - ra);
  // seen from the ground rather than the earth's centre: up to a degree lower
  p.elevation -= static_cast<float>(e.parallax * std::cos(radians(p.elevation)));
  return p;
}

}  // namespace p64::widgets::solar

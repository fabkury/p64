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

float moon_phase(int year, int year_day, float hour_local) {
  const double days = (year - 2000) * 365.25 + year_day + 1 - 6.76 + hour_local / 24.0;
  double phase = std::fmod(days / 29.530588, 1.0);
  if (phase < 0) phase += 1.0;
  return static_cast<float>(phase);
}

Position moon(float latitude, float longitude, float tz_hours, int year_day, float hour_local, float phase) {
  const double g = 2 * kPi / 365 * (year_day + phase * 365.25);
  const double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) +
                      0.000907 * std::sin(2 * g);
  const double g0 = fractional_year(year_day, hour_local, tz_hours);
  const double eqtime = 229.18 * (0.000075 + 0.001868 * std::cos(g0) - 0.032077 * std::sin(g0));
  const double tst = hour_local * 60.0 + eqtime + 4.0 * longitude - 60.0 * tz_hours;
  const double ha = radians(tst / 4 - 180 - 360.0 * phase);
  return elevation_azimuth(radians(latitude), decl, ha);
}

}  // namespace p64::widgets::solar

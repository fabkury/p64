// Pure text formatting for the clock faces and the overlay (host-tested).
#pragma once

#include <ctime>
#include <string>

namespace p64::widgets::clock_format {

// "14:05", "14:05:09", " 2:05" (12-hour, no leading zero), colon blanked when `colon` is false.
std::string time_text(const tm &t, bool h24, bool seconds, bool colon = true);
// "AM"/"PM" in 12-hour mode, "" otherwise.
std::string meridiem(const tm &t, bool h24);
// "Sat 19 Sep" or "Sat Sep 19".
std::string date_text(const tm &t, bool month_first);
const char *weekday_short(int wday);  // "Sun".."Sat"
const char *month_short(int mon);     // "Jan".."Dec"

}  // namespace p64::widgets::clock_format

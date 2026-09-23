---
status: accepted
date: 2026-09-23
---

# The time is trusted only once an NTP server has answered in the current boot

Nothing but an NTP answer makes the wall clock trusted. The on-board PCF85063A RTC is no
longer used (its driver is gone) and "set time from this browser" is removed. At boot, on
every Wi-Fi connection and every 6 hours, SNTP asks the server the router offers over
DHCP (option 42), then the configured server (default `pool.ntp.org`), then
`time.google.com` and `time.cloudflare.com`. An answer earlier than the firmware's build
date is refused before it reaches the system clock. Until the first accepted answer the
time is unknown: clocks show `--:--`, and Makapix, the night schedule, the cache sweep and
the loader's "last played" touch wait. Once trusted, the time stays trusted until the next
reboot, even if NTP stops answering: the device's clock drifts well under a second a day,
and the status shows the age of the last answer.

Why: the board's RTC has no battery (Waveshare only reserves a connector for one), so a
cold boot finds its oscillator-stop flag set and it knows nothing; a manual set took
whatever time a browser had and made it trusted, then persisted it. A wrong trusted time
is not harmless: the cache sweep (ADR 0010) deletes by file date, and a clock set a year
back or ahead would have deleted the whole cache at the next night, on exactly the
installs without internet that the manual set existed for, which cannot download it
again. NTP is the one source whose time the device can check (against the build date and
by asking several servers).

The sweep gains two guards. A file date before the file floor (the build date, minus a
day for the build machine's time zone, minus 366 days for the longest retention) was
written under an untrusted clock (FAT stamps 1980 when the clock reads 1970) and is
deleted; the floor sits a retention period below the build date so a firmware update
never makes a recently played file look implausible. A file dated more than a day in the
future means the clock or the card is not what it seems, and the sweep then deletes
nothing at all (it used to delete that file as "written under a wrong clock").

Considered and rejected: keeping the RTC for display only, with a separate untrusted clock
for the clocks and the night schedule (two levels of time, more states, for a chip that
knows nothing after a power cut); falling back to the `Date` header of an HTTPS response
from makapix.club when NTP is blocked (the principle is one trusted source; a blocked
network is reported instead); letting the trust lapse after a day without an answer (the
drift does not justify stopping Makapix and the clocks); a floor of exactly the build date
for file dates (every firmware update would delete every file not played since the build).

Consequences: a network that blocks UDP port 123 and offers no NTP server over DHCP leaves
the device without trusted time: no clocks, no Makapix, no sweep. The log warns 60 s after
the connection and every 30 minutes, and the web UI's Settings page shows "waiting for
NTP". A file written to the card before the first answer (an upload in the first seconds
after a cold boot) carries FAT's 1980 date: in a local folder it sorts as the oldest, an
accepted deviation. lwIP writes the router's server into slot 0 and clears the other
slots on every DHCP ACK, lease renewals included, without any event when the address
stays the same, so the clock repairs the slots on a 30 s timer as well as on connection
changes.

# Widget research: new data sources and the competition

Research of 2026-10-03 (prompt p073): the material for a discussion about which widgets
p64 could grow next. Since then the shortlist's first entry, air quality and UV, was
built the same day (prompt p074, spec 7.4); nothing else here is implemented or decided.

Scope, as settled with the user before the research:

- Sources: keyless public APIs, and services that need a user-supplied key, token or
  OAuth (full OAuth acceptable where it is technically feasible).
- Architecture: **device-direct only**. The device fetches and renders by itself; no
  companion server, no project-run cloud.
- Subjects looked at hardest: sky, earth and nature; money and news; transit, sport and
  travel.
- Audience: any p64 builder, worldwide, so global coverage and little setup rank high.
- Competition: commercial pixel displays, open firmware projects, adjacent smart displays.

The files:

| File | What it holds |
|---|---|
| [competition.md](competition.md) | What comparable products ship as widgets and how they work technically |
| [keyless-sources.md](keyless-sources.md) | Public APIs that need no account or key, with measured response sizes |
| [keyed-sources.md](keyed-sources.md) | Key-paste and OAuth services, and what OAuth can be without a backend |

How far to trust the numbers: the keyless catalogue's sizes were measured on 2026-10-03
with plain `curl` from a US address. The keyed survey marks each claim as read on the
provider's documentation, taken from a search summary, or from memory; free-tier figures
change often and every one should be rechecked before anything is built on it. Nothing
was tested from the device.

## What the competition teaches

1. **The widget types everyone has**, in rough order of how often they appear: clock,
   weather, countdown/timer, follower counters, stocks/crypto/currency, sports scores,
   calendar/agenda, transit arrivals, now-playing with album art, "show my own data",
   message board, news, moon/sun, air quality, flights overhead. p64 has the first two
   (and astronomy inside the horizon face) and nothing of the rest.
2. **Almost nobody does it on the device.** Tidbyt/Tronbyt and TRMNL render on a server
   and send an image; AWTRIX, PixelIt and ESPHome are dumb renderers fed by Home
   Assistant or Node-RED; Divoom leans on its own cloud. The projects that fetch and
   render on a microcontroller are Pixelix, Adafruit's MatrixPortal guides and
   witnessmenow's libraries. p64's device-direct rule puts it in that smaller group:
   every widget is firmware code and every key sits on the device, but nothing dies when
   a server does (Tidbyt's end is the cautionary tale).
3. **The most used plugin, wherever it exists, is "poll my URL and show the value"**:
   TRMNL's Private Plugin (first by connections), LaMetric's My Data DIY, Pixelix's
   GrabViaRest. It is device-direct (the device polls; whatever answers is the owner's
   business), so it is inside the rule, and it is the one feature that lets others extend
   p64 without firmware changes.
4. **64x64 is a rare advantage.** Most rivals are 32x8 or 64x32 and reduce everything to
   icon plus scrolling text. Maps, boards, graphs and pictures (radar, chessboard, sky
   chart, contribution graph, album art) are what the small ones cannot do.
5. **Sports is Tidbyt's biggest category and has no microcontroller-only example** in
   the survey; the data is large, league-specific and mostly unofficial.

## What the sources survey teaches

- **Keyless and good**: Open-Meteo's other endpoints (air quality, UV, marine with a
  global tide curve), USGS earthquakes, NOAA SWPC space weather, ISS position and TLEs,
  RainViewer radar tiles (PNG), Frankfurter currency rates, Coinbase/CoinGecko/mempool
  for crypto, Nager.Date holidays, Jolpica F1, MLB (with a field filter), Lichess.
  Responses of 0.1 to 5 KB, cJSON-friendly.
- **Keyless but awkward**: flights overhead (11 to 43 KB, grows with traffic), TfL
  arrivals (27 KB), RSS headlines (15 to 32 KB of XML), Wikipedia "on this day"
  (138 KB), ESPN scores (76 to 287 KB, unofficial). All need a streaming parser.
- **No legitimate keyless stock quotes exist.** Stooq is behind a key since 2026; Yahoo
  is unofficial and against its terms. Stocks mean a pasted Finnhub key.
- **OAuth without a backend is clean on four services**: Microsoft Graph, GitHub, Twitch
  and Trakt accept the device grant with a public client ID and no secret. Google's
  device flow excludes Calendar. Spotify is effectively closed to an open-source project
  (five users per app, Premium, HTTPS or loopback redirects only), so each owner would
  register an app and paste a URL back; Last.fm gives now-playing with one key instead.
- **Calendar's practical route is the private ICS URL**; the cost is a recurrence
  engine and feeds of up to several MB through the single TLS slot.
- **Transit has no worldwide keyless source.** Transitland (key, 10,000 queries a month)
  is the widest aggregator; TfL, Switzerland and Norway are keyless.

## Three enablers that matter more than any one widget

| Enabler | What it unlocks | Cost to look at |
|---|---|---|
| A baseline JPEG decoder | Album art, daily museum art, APOD, satellite and sun images, thumbnails: nearly every image source serves only JPEG | Flash size, work RAM (expected small, unmeasured), a host-tested decoder path like the other three |
| A streaming JSON/XML reader | Flights, sports, TfL, RSS, Wikipedia: anything over ~16 KB that cJSON should not hold whole | A pure, host-testable file; no new RAM class |
| One shared fetch scheduler for widgets | Many small pollers sharing the single TLS slot politely, each with its cadence, its "last good value with age" and its stale rule (what weather does alone today) | Design work; every new host is a 12 to 13 KB TLS session while it lasts, one at a time |

Also common to all: an identifying `User-Agent` (Wikimedia, MET Norway, NWS and GitHub
require one), attribution lines in the web UI's About section, and trusted time before
any dated request (ADR 0011).

## Ranked shortlist

Ranked for any p64 builder by appeal against cost on p64 (internal RAM, the TLS slot,
legibility on 64x64, setup). This is a recommendation to argue with, not a plan.

| # | Widget | Source | Key | Why here | Main cost or risk |
|---|---|---|---|---|---|
| 1 | Air quality and UV | Open-Meteo air quality | none | Global, ~600 B, same provider and terms as weather; could be a page of the weather widget or its own | Pollen is Europe-only |
| 2 | Rain radar | RainViewer tile centred on the location | none | The most 64x64-native idea: one 10 to 16 KB PNG box-averaged 4x, animated from two hours of frames; no small rival can show it | Needs a base map (coastline) drawn on-device; radar coverage is not global; terms are personal use with attribution, no availability promise |
| 3 | Markets ticker | Coinbase or CoinGecko (crypto), Frankfurter (currency); Finnhub for stocks | none; a key for stocks | Fifth most common widget everywhere; 60 to 220 B responses; a sparkline has room on 64x64 | Stocks need a key; Binance is geo-blocked; history for the sparkline must be kept on the device |
| 4 | Sky tonight | SWPC Kp and scales, ISS TLE from CelesTrak with SGP4 on the device, planets computed | none | Extends what the horizon and orrery faces began; aurora chance from Kp and latitude; "ISS overhead in 12 min" is an event worth an interlude | SGP4 is new pure code to write and test; wheretheiss.at is one person's service |
| 5 | Countdown and holidays | User dates in settings; Nager.Date for the next public holiday | none | Third most common type across products; nearly no network | Mostly on-device, so partly outside this research's scope |
| 6 | Planes overhead | OpenSky (anonymous 400 credits a day, or the owner's client credentials for 4,000), adsb.lol | none or a pasted pair | High charm on an LED matrix, proven on ESP32 HUB75 | 11 to 43 KB and growing with traffic: wants the streaming reader; no route or aircraft type without a second source; non-commercial terms |
| 7 | Earthquakes | USGS query, radius and magnitude filter, 475 B as text | none | Official, public domain, global | Dull when nothing happens: better as an event that triggers an interlude than as a standing widget |
| 8 | Tides and waves | Open-Meteo marine | none | The only global keyless tide curve; a tide graph suits the panel | Coastal owners only; a model, not a harbour gauge |
| 9 | Generic "my data" | A URL the owner enters; a small fixed JSON shape (value, label, icon, series) | owner's own | The most used plugin on every platform that has one; opens Home Assistant, Apps Script and anything else without the project running a server | A contract to design and keep; plain-HTTP LAN sources and HTTPS ones both to serve |
| 10 | Calendar agenda | Private ICS URL (Google, Outlook, iCloud); Microsoft Graph by device flow as a second step | a pasted URL | The highest-appeal keyed source in every survey | A recurrence expander (pure, testable, but real work); multi-MB feeds hold the TLS slot 10 to 20 s; outside the three subjects chosen for depth |
| 11 | Next departures | TfL, Swiss, Entur keyless; Transitland with a key | mixed | Second-largest Tidbyt category | Per-region adapters and upkeep; finding a stop ID is the owner's burden; 10,000 queries a month on Transitland |
| 12 | Sport | Jolpica F1 (keyless, 0.8 to 2.4 KB); MLB with `fields=` | none | The clean ones are narrow | Broad live scores exist only as ESPN's unofficial 76 to 287 KB responses or behind paid tiers |
| 13 | Headlines | RSS (BBC, NPR, Al Jazeera) | none | Common on larger displays | Text-heavy content is the weakest fit for 64 px; streaming XML; publisher terms |

Noted but outside the subjects chosen for depth: Last.fm now-playing (one key; album art
needs the JPEG decoder unless its CDN really serves PNG, untested), the GitHub
contribution graph (device flow; 53x7 cells suit the panel), follower counters (Bluesky
and Mastodon keyless, YouTube with a key), the Lichess daily puzzle.

My own reading: 1 and 3 are the cheap wins on existing machinery; 2 and 4 are the ones
that would make p64 look like nothing else; 9 is the strategic one; the JPEG decoder and
the fetch scheduler are worth deciding before any of them, because they change what the
rest costs.

## Open questions for the discussion

- Are new sources new **widgets** (each with a main-state slot and an interlude gap), or
  **pages** of fewer widgets (weather grows air quality, radar, tides)? The settings and
  interlude model of spec 6.1 scales poorly to a dozen kinds.
- Event-driven interludes ("ISS overhead", "earthquake nearby", "aurora likely") do not
  exist in the spec; today interludes are a dice roll at each swap.
- How many pollers may be live at once against the 48 KB floor, and is there a ceiling on
  distinct TLS hosts per hour?
- Does a pasted key deserve the PIN to be mandatory, given the web UI is plain HTTP?
- Is the unofficial-source line (ESPN, Yahoo) "never" or "opt-in and labelled"?

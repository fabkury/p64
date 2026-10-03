# Keyed and OAuth sources, device-direct

A survey of services that need a key, a token or OAuth from the owner, and what is
feasible on a microcontroller with no backend. Researched 2026-10-03. Part of
[the widget research](README.md).

**Evidence labels.** [V] read on the provider's live documentation that day; [S] from a
search-result summary, not opened at the primary source; [M] from memory, unverified.
Free-tier numbers move often: recheck every [S] and [M] figure before building on it.

The constraints: the firmware is public, so no secret can be embedded; there is no
project server to hold a client secret or receive a redirect; the web UI is plain HTTP
on the LAN at `http://p64.local`; secrets would live in NVS.

## 1. OAuth with no backend

### Two patterns

- **A. The project ships a public client ID.** Needs a flow without a secret: the device
  grant (RFC 8628: the panel or the web UI shows a code, the owner types it on the
  provider's site) or PKCE. Works only where the provider allows secret-less clients,
  does not cap an unreviewed app's users, and tolerates a client ID in a public
  repository.
- **B. Each owner registers their own app** and pastes its client ID (and secret) into
  the web UI. Always works; costs the owner 10 to 20 minutes on a developer console.
  Almost all hobby firmware does this.

Authorization-code services add a redirect problem: `http://p64.local/callback` is plain
HTTP on a non-loopback host, which most providers now refuse. The backend-less
workaround is **paste-back**: register `http://127.0.0.1:<port>/callback`, let the
browser land on a dead page after consent, and have the owner paste the address-bar URL
into the p64 web UI, where the device exchanges the code. Clumsy, but it needs no TLS
server on the device.

### Service by service

| Service | Device grant | Secret needed | Public client ID workable | Notes |
|---|---|---|---|---|
| Microsoft Graph | Yes [V] | No [V] | Yes, with public client flows enabled [S] | `offline_access` gives a refresh token; calendar and To Do on personal accounts should work [M]; work tenants often block device code [M] |
| GitHub | Yes [V] | No [V] | Yes [V] | Respect the returned polling interval |
| Twitch | Yes [V] | No [V] | Yes | Refresh token single-use, dies after 30 days unused [V] |
| Trakt | Yes [V] | No [V] | Yes | Token rules changed twice in 18 months [S] |
| Google | Yes, but only for profile, Drive file and YouTube scopes [V] | Yes, sent when polling [V] | In principle | **Calendar is not on the device-flow list** [V]; unverified apps' refresh tokens die after 7 days [S] |
| Spotify | Allowlisted partners only [S] | PKCE needs none | **No, in practice** | See below |
| Strava | No [S/M] | Yes [M] | No | Pattern B with paste-back |
| Fitbit | n/a | n/a | No | Web API switched off 2026-10-30; the successor uses Google OAuth without device-flow health scopes [S, V] |
| Withings, Tesla | No | Yes, plus a public HTTPS endpoint | No | Not feasible without a backend [S] |
| Todoist | Not needed | n/a | n/a | A personal token that does not expire [S] |
| Home Assistant | Not needed | n/a | n/a | A long-lived token on the LAN [M]; a bridge to anything it integrates |

**Spotify in detail.** Redirect URIs must be HTTPS, except the loopback literals
`http://127.0.0.1:PORT` and `http://[::1]:PORT`; `localhost` is refused [V]. An app in
development mode has at most five allowlisted users and its owner must have Premium [V];
since early 2026 each developer gets one such client ID with a reduced endpoint set [S].
Extended quota is for registered businesses with at least 250,000 monthly users [V]. So
a project-wide client ID is impossible: every owner would need Premium, their own app
and the paste-back flow, and the album art is JPEG [M], which p64 does not decode.

### What other open firmware does

- **spotify-api-arduino**: pattern B, with a one-off sketch serving a callback at the
  ESP's IP; that redirect predates the 2025 rules and would now be refused.
- **ESPHome, AWTRIX 3**: no OAuth clients at all; Home Assistant or Node-RED is the
  backend.
- **Tidbyt/Pixlet, TRMNL**: OAuth in their cloud. The model p64 excludes.

No mainstream open firmware was found doing OAuth device-direct with a shipped client
ID (not searched for specifically). p64 would be unusual in doing so for Microsoft,
GitHub, Twitch and Trakt, where it is clean.

## 2. Calendar without OAuth

- **Private ICS URLs**: Google's "secret address in iCal format", Outlook's published
  `.ics` link, iCloud's public calendar (`webcal://` becomes `https://`; "public" there
  means anyone with the link) [S].
- **Size**: Google's feed is the whole calendar, with no date-range parameter [M];
  expect 100 KB to several MB. A streaming parse in constant memory is feasible: unfold
  lines, one `VEVENT` at a time, keep only the next N days.
- **The hard part is recurrence**: `RRULE`, `EXDATE`, `RECURRENCE-ID` overrides and
  `VTIMEZONE`. A pure, host-tested expander for daily, weekly, monthly and yearly rules
  with BYDAY, COUNT and UNTIL covers most calendars.
- **Cost on p64**: a 2 MB feed at 100 to 200 KB/s holds the TLS slot for 10 to 20 s;
  poll every 15 to 30 minutes.
- **CalDAV with app passwords** (iCloud, Fastmail, Nextcloud) allows a server-side
  time-range query and small responses, at the price of XML and discovery round-trips
  [M]. Google's CalDAV requires OAuth [M].
- **The Apps Script trick**: the owner pastes a script into script.google.com, deploys
  it as a web app, and it returns trimmed JSON [S]. It is a mini backend, but the
  owner's own; p64 could document it as one use of a generic "JSON URL" source without
  running anything itself.

## 3. Key-paste services

### Stocks

| Service | Free tier | Notes |
|---|---|---|
| **Finnhub** | 60 calls a minute, real-time US quotes [S] | `GET /api/v1/quote?symbol=AAPL&token=…`, about 100 B. Best fit |
| Twelve Data | 800 credits a day [S] | Global symbols, forex |
| Alpha Vantage | 25 requests a day [S] | Too few |
| Polygon ("Massive") | 5 calls a minute, delayed [S] | Daily closes only |
| FMP | 250 a day [S] | Marginal |
| marketstack | 100 a month [S] | Useless for polling |

### News

The Guardian Open Platform (500 a day, non-commercial, small JSON with `page-size`) is
the best keyed source [S]; NYT gives 500 a day but tens of KB per response [S, M].
NewsAPI.org's free tier is for development only, so it is out. Keyless RSS stays the
wider and safer route.

### Transit

- **Transitland v2** is the widest single aggregator:
  `GET https://transit.land/api/v2/rest/stops/{stop_key}/departures`, scheduled and
  real-time values [V], with a key [S] and 10,000 queries a month free [S]: one poll
  every 4.5 minutes around the clock, or one a minute inside commute windows.
- Transit App's free tier (1,500 a month) is too thin [S].
- National and regional: Deutsche Bahn (key, XML), NS (key, 5,000 a day), SNCF (5,000 a
  day), UK Realtime Trains (new API since 2026-03, 9,000 a day), Digitransit Finland
  (key, GraphQL) [S]. Keyless: transport.opendata.ch, Entur (wants a client-name
  header), TfL [M]. US agencies: MBTA keyless at a low rate, WMATA and CTA with free
  keys [M].
- Commute time: Google Routes needs a key and a billing account [M].

### Flights

- **OpenSky** with an account: OAuth2 client credentials only since 2026-03 [S]; the
  owner pastes a client ID and secret, tokens last about 30 minutes, 4,000 credits a day
  (400 anonymous) [S]. The best "planes overhead" source; non-commercial terms [M].
- FlightAware AeroAPI: a personal tier with $5 of monthly credit and a card on file [S];
  a hard cap would be needed. Flightradar24 has no free tier [S].

### Sport

football-data.org (10 a minute, delayed scores) and API-Football (100 a day, live
fixtures) are the usable free tiers [S]; neither gives broad live scores. Hobby boards
use ESPN's undocumented keyless JSON instead.

### Media and social

- **Last.fm**: `user.getRecentTracks` with `limit=1`; the playing track carries
  `nowplaying="true"` [S]. A key and a username, no user auth, 1 to 2 KB. Works for any
  player that scrobbles, Spotify included. Art URLs come in 34, 64, 174 and 300 px with
  `.png` or `.jpg` extensions [S]; whether the CDN really serves PNG must be tested on
  the device.
- **YouTube Data API v3**: a key from the Google Cloud console;
  `channels.list?part=statistics` costs 1 of 10,000 daily units [S]; counts are rounded
  [M].
- Twitch by device flow ("who is live"); Bluesky and Mastodon are keyless.
- Instagram, TikTok and X are closed or paid for this use [M]. Apple Music needs a
  developer-signed token.

### Productivity and development [mostly M]

GitHub (notifications, the contribution calendar through GraphQL, Actions status: a
53x7 graph suits the panel), Todoist, GitLab, Linear, Trello, healthchecks.io, Uptime
Kuma, Plausible, Stripe with a restricted key. All are small bearer-token JSON calls.
The risk is a powerful token on a device with a plain-HTTP UI: recommend the narrowest
scopes.

### Nature with keys

WAQI/AQICN (free token, about 2 KB, attribution) and IQAir (10,000 calls a month) for
station-measured air quality [S]; Stormglass (10 requests a day, enough for tide
extremes) and WorldTides (paid after 100 credits) for tides [S]; N2YO for satellite
passes (100 an hour) [S]; OpenWeatherMap (60 a minute on the classic plan) [S]; NASA
(a registered key gives 1,000 an hour; APOD is JPEG) [S].

### A language model

Feasible: one HTTPS POST with a pasted key, a response of a few KB. It can hold the TLS
slot for 5 to 30 s and the owner pays per call, so once or a few times a day. The risk
is a billable key stored on the device.

## 4. Verdicts for p64

| Family | Device-direct | Setup burden (1 to 5) | Free tier | Main risks |
|---|---|---|---|---|
| Microsoft calendar and To Do | Yes | 1 with a project client ID | Ample | Work tenants blocking; large tokens |
| Google Calendar by ICS | Yes, with caveats | 2 | Unmetered | Feed size, recurrence, a secret URL in NVS |
| Google Calendar by OAuth | No, in practice | 5 | n/a | Scope not on the device flow |
| CalDAV with an app password | Yes, with caveats | 3 | Ample | XML, discovery |
| Spotify now playing | Yes, with caveats | 5 | Fine | Premium, an app per owner, paste-back, JPEG, churn |
| Last.fm now playing | Yes | 2 | Fine | Needs scrobbling; art format to confirm |
| GitHub | Yes | 1 or 2 | 5,000 an hour [M] | None notable |
| Twitch, Trakt | Yes | 1 | Ample | Refresh tokens expire when unused |
| Stocks (Finnhub) | Yes | 2 | Ample | US-centric |
| News (Guardian) | Yes | 2 | Fine at 15 min | Non-commercial terms |
| Transit (Transitland, local) | Yes, with caveats | 3 | Tight | Coverage varies; finding the stop ID |
| Flights (OpenSky) | Yes | 3 | Fine in windows | Non-commercial |
| Sport | Yes, with caveats | 2 | Poor for live | Live data paywalled |
| YouTube counters | Yes | 3 | Ample | Rounded counts |
| Air quality (WAQI, IQAir) | Yes | 2 | Ample | Attribution |
| Tides (Stormglass) | Yes | 2 | A few a day | 10 a day |
| Strava | Yes, with caveats | 5 | Fine | Secret and paste-back |
| Fitbit, Withings, Tesla | No | n/a | n/a | Backend or domain required |
| Home Assistant | Yes | 2 | Unmetered | Needs Home Assistant |

**The JPEG gap** blocks Spotify art, YouTube thumbnails, APOD and webcams. A baseline
decoder (TJpgDec or Espressif's `esp_jpeg`, a few KB of work RAM [M]) would unlock more
sources than any single integration.

## 5. Device-direct precedents on ESP32

- Spotify: https://github.com/witnessmenow/spotify-api-arduino
- YouTube: https://github.com/witnessmenow/arduino-youtube-api
- Stocks with Finnhub: https://github.com/GainedNirvana/ESP32-StockTicker,
  https://github.com/mike-rankin/ESP32_Stock_Ticker
- Transit: https://github.com/gadec-uk/departures-board (National Rail, TfL),
  https://github.com/mgaman/TFL-tube-arrivals-board-ESP32-Arduino,
  https://github.com/alopes/esp32-tfl-bus-led
- Flights: https://github.com/peterdaley/TheFlightWall_OSS (HUB75, OpenSky and AeroAPI),
  https://github.com/emir173/ESP32-Flight-Tracker
- Google Calendar through Apps Script: https://github.com/kristiantm/eink-family-calendar-esp32

## The ten best, by appeal, feasibility and low burden

1. Calendar by private ICS URL: the highest appeal, one pasted URL; the cost is the
   recurrence engine.
2. Last.fm now playing: a key and a username; covers Spotify listeners without
   Spotify's restrictions.
3. Finnhub stock ticker: one key, tiny responses, real-time on the free tier.
4. GitHub (contribution graph, notifications, CI) by device flow with a project client
   ID.
5. Microsoft calendar and To Do by device flow: the cleanest full OAuth available.
6. Station air quality (WAQI or IQAir).
7. Next departures: Transitland as the generic source, keyless TfL, Swiss and Entur
   where they apply.
8. Planes overhead with OpenSky client credentials.
9. YouTube or Twitch counters and live status.
10. A Home Assistant entity: the escape hatch for everything p64 cannot reach directly.

Spotify stays outside despite its appeal.

## Sources

- Google device flow scopes: https://developers.google.com/identity/protocols/oauth2/limited-input-device
- Microsoft device code: https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code
- Spotify redirect URIs: https://developer.spotify.com/documentation/web-api/concepts/redirect_uri
- Spotify quota modes: https://developer.spotify.com/documentation/web-api/concepts/quota-modes
- GitHub device flow: https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps
- Twitch: https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/
- Trakt: https://developer.trakt.tv/docs/authentication-oauth
- Transitland: https://www.transit.land/documentation/rest-api/departures and https://www.transit.land/plans-pricing
- OpenSky: https://openskynetwork.github.io/opensky-api/rest.html
- FlightAware: https://www.flightaware.com/commercial/aeroapi/
- football-data.org: https://docs.football-data.org/general/v4/policies.html
- Last.fm: https://www.last.fm/api/show/user.getRecentTracks
- WAQI: https://aqicn.org/api/
- NASA: https://api.nasa.gov/
- Google Calendar ICS: https://support.google.com/calendar/answer/37648
- Realtime Trains: https://blog.realtimetrains.com/2026/03/next-generation-api-now-available/

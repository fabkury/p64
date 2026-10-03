# Keyless public data sources

A catalogue of APIs that need no account and no key, checked on 2026-10-03. Sizes are
uncompressed bytes measured that day with plain `curl` from a US address, unless marked
otherwise; "unverified" means the claim could not be confirmed on a primary page. All
are HTTPS unless stated. Nothing was fetched from the device. Part of
[the widget research](README.md).

The device's limits that shape the verdicts: one TLS session at a time (12 to 13 KB of
internal RAM while it lasts), cJSON parsing (so small responses, ideally under 16 KB),
decoders for GIF, PNG and WebP but **not JPEG**. Most image sources serve only JPEG.

## A. Sky, earth and nature

### Open-Meteo's other endpoints

Same terms as the forecast API p64 already uses: under 10,000 calls a day, non-commercial,
CC BY 4.0 attribution ([terms](https://open-meteo.com/en/terms)). Global JSON; `current=`
keeps them tiny.

| Endpoint | Example | Bytes | Notes |
|---|---|---|---|
| Air quality | `https://air-quality-api.open-meteo.com/v1/air-quality?latitude=52.52&longitude=13.41&current=european_aqi,us_aqi,pm2_5,uv_index,grass_pollen,birch_pollen,alder_pollen,ragweed_pollen&timezone=auto` | 611 | AQI, PM and UV are global; pollen is Europe-only per the docs (not re-checked) |
| Marine | `https://marine-api.open-meteo.com/v1/marine?latitude=54.54&longitude=10.23&current=wave_height,wave_direction,wave_period,sea_surface_temperature,sea_level_height_msl` | 530 | `sea_level_height_msl` includes tides: the only global keyless tide source found. A model, not a harbour gauge. 48 hourly values: 1,486 B |
| Flood | `https://flood-api.open-meteo.com/v1/flood?latitude=59.91&longitude=10.75&daily=river_discharge&forecast_days=7` | 394 | River discharge; only meaningful on a river |
| Sun and UV | forecast API with `daily=sunrise,sunset,daylight_duration,uv_index_max` | 452 | Redundant with `solar.cpp` except UV |
| Lightning | forecast API with `current=lightning_potential,cape` | 362 | `lightning_potential` was `null` for New York; a model proxy, not strikes |

### ISS and space

| Source | Example | Bytes | Verdict |
|---|---|---|---|
| wheretheiss.at | `https://api.wheretheiss.at/v1/satellites/25544` | 313 | Good; about 1 request/s; includes eclipsed/daylight; `/positions?timestamps=` gives a ground track. One person's project |
| open-notify | `http://api.open-notify.org/iss-now.json`, `/astros.json` | 114 / 587 | HTTP only (HTTPS failed); pass prediction retired. Ageing |
| CelesTrak | `https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE` | 168 | The route to pass predictions: fetch every few hours, run SGP4 on the device |
| people in space | `https://corquaid.github.io/international-space-station-APIs/JSON/people-in-space.json` | 8,339 | Static, hand-maintained |
| RocketLaunch.Live | `https://fdo.rocketlaunch.live/json/launches/next/1` | 1,442 | Next launch; attribution terms not checked |
| Launch Library 2 | `https://ll.thespacedevs.com/2.3.0/launches/upcoming/?limit=1&mode=list` | 1,295 | 15 requests/hour per IP (unverified) |

### Earthquakes

- **USGS FDSN query**: official, global, public domain, filterable.
  `https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&latitude=35.68&longitude=139.69&maxradiuskm=500&minmagnitude=4&limit=3&orderby=time`
  returned 2,450 B; `format=text` gives the same events as pipe-separated lines in 475 B,
  no JSON parser needed.
- USGS summary feeds are fixed files (`significant_week.geojson` 1,869 B,
  `4.5_day.geojson` 11,465 B); not to be polled more than about once a minute.
- **EMSC** is the European alternative with the same syntax
  (`https://www.seismicportal.eu/fdsnws/event/1/query?limit=3&format=json&minmag=5`, 1,729 B).

### NOAA SWPC space weather

Official, public domain, no documented rate limit. Files under
`https://services.swpc.noaa.gov`:

| File | Bytes | Fit |
|---|---|---|
| `/products/noaa-scales.json` (R/S/G now and three days) | 1,097 | Best single file |
| `/products/noaa-planetary-k-index.json` (7 days of Kp) | 4,784 | Good |
| `/products/noaa-planetary-k-index-forecast.json` | 6,905 | Good |
| `/products/summary/solar-wind-speed.json` | 59 | Good |
| `/products/summary/solar-wind-mag-field.json` | 60 | Good |
| `/text/3-day-forecast.txt` | 1,851 | Plain text |
| `/products/alerts.json` | 36,974 | Too big |
| `/json/ovation_aurora_latest.json` (aurora grid) | 918,513 | Poor |

An aurora widget is best derived from Kp and the owner's geomagnetic latitude.

### Tides, rivers, sun and moon

- **NOAA CO-OPS** (US only): today's highs and lows for a station in 223 B
  (`https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?date=today&station=8518750&product=predictions&datum=MLLW&time_zone=lst_ldt&interval=hilo&units=metric&format=json`).
  The owner picks a station ID.
- Global tides: only Open-Meteo marine.
- River levels are national: USGS water services (2,405 B), the UK Environment Agency
  (1,829 B), France's Hub'Eau (897 B). No global source.
- sunrise-sunset.org, MET Norway (needs an identifying User-Agent) and USNO work but are
  redundant with the on-device maths.
- Meteor showers and planet visibility: no API worth using. Showers are a static annual
  table to bake in; planets are computable with the low-precision series already used
  for the moon.

### Hazards

- **NWS alerts** (US only): `https://api.weather.gov/alerts/active?point=40.71,-74.01`,
  229 B with no alerts; one alert is several KB (estimated). Requires a User-Agent.
- **MeteoAlarm** (Europe): 1 MB per country feed, no location filter. Poor.
- **NASA EONET** (wildfires, volcanoes, storms): 785 B with `limit=1`; bounding box
  only; wildfire entries skew to the US.
- GDACS (65 KB) and NHC (9.5 KB, no filter) are poor fits. Blitzortung has no public API.

### Imagery

- **RainViewer radar**: works for a 64x64 map from one tile. The index
  `https://api.rainviewer.com/public/weather-maps.json` (818 B) lists about two hours of
  frames; the tile `{host}{path}/256/{z}/{lat}/{lon}/{color}/{smooth}_{snow}.png` is
  centred on the owner's coordinates. Measured `/256/6/40.71/-74.01/2/1_1.png`:
  15,652 B, 256x256, 8-bit RGBA PNG; a 4x box average gives 64x64. Maximum zoom 7.
  `.webp` also answered (12,174 B) but is undocumented. No base map: echoes on
  transparency. Terms: personal or educational use, attribution with a link, no
  availability promise ([API page](https://www.rainviewer.com/api.html)). Coverage is
  wherever radar exists.
- **Himawari** (NICT): a 550x550 PNG of 452,659 B; decodable into PSRAM but a heavy
  download; non-commercial with credit (unverified).
- JPEG only, so out today: NASA EPIC thumbnails (5.6 KB), GOES full disk (129 KB), the
  SDO sun (12.8 KB), NASA APOD (also needs a key; `DEMO_KEY` returned HTTP 500 twice).

## B. Money and news

### Crypto and currency

| Source | Example | Bytes | Notes |
|---|---|---|---|
| Coinbase | `https://api.coinbase.com/v2/prices/BTC-USD/spot` | 61 | Smallest, official, no geo-block seen |
| CoinGecko keyless | `https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum&vs_currencies=usd,eur&include_24hr_change=true` | 222 | Widest coverage; a shared pool of roughly 10 to 30 calls a minute; attribution requested |
| Kraken | `https://api.kraken.com/0/public/Ticker?pair=XBTUSD` | 308 | Official |
| Binance | `https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT` | — | HTTP 451 from a US address; a poor global default |
| mempool.space | `/api/blocks/tip/height`, `/api/v1/fees/recommended`, `/api/v1/prices` | 6 / 74 / 108 | Open source; limits undocumented |
| Frankfurter | `https://api.frankfurter.dev/v1/latest?base=USD&symbols=EUR,BRL,JPY,GBP` | 111 | ECB daily rates, about 30 currencies. Best currency fit |
| open.er-api.com | `https://open.er-api.com/v6/latest/USD` | 2,972 | About 160 currencies, daily; attribution required |

CoinCap is dead as a keyless source.

### Stocks

There is no legitimate, stable keyless source. **Stooq** requires a key behind a CAPTCHA
since spring 2026. **Yahoo**'s chart endpoint answered (1,295 B) but is unofficial,
against Yahoo's terms, and has broken clients repeatedly; if offered at all it would be
opt-in and labelled. See [keyed-sources.md](keyed-sources.md) for Finnhub.

### News and reference

- **RSS** is keyless but XML, so it wants a streaming reader that stops after N titles:
  BBC World 32,117 B, NPR 15,156 B, Al Jazeera 17,342 B; the Guardian's is 148 KB;
  Reuters' public feeds are gone. Headlines are the publishers' copyright.
- **Hacker News**: Algolia's `https://hn.algolia.com/api/v1/search?tags=front_page&hitsPerPage=5`
  gives the front page in one call (6,223 B).
- **Wikipedia** "on this day": 138 KB with no field filter (the featured feed 295 KB);
  usable only with a streaming reader. Wikimedia requires a descriptive User-Agent.
- **Reddit**: `.json` returned 403 for every User-Agent tried. Unreliable.
- Quotes and facts: ZenQuotes (299 B, attribution), Useless Facts (308 B). Quotable,
  Numbers API and dictionaryapi.dev were dead that day; no reliable keyless word of the
  day was found, so a bundled list is safer.
- **GitHub** unauthenticated: 60 requests an hour per IP, 5 KB per repository, needs a
  User-Agent. Enough for a star counter.

## C. Transit, sport and travel

### Flights overhead

| Source | Endpoint | Bytes (New York, about 85 aircraft) | Limits and terms |
|---|---|---|---|
| adsb.lol | `https://api.adsb.lol/v2/point/{lat}/{lon}/{nm}` | 39,428 at 15 nm | Keyless, ODbL; limits not found |
| adsb.fi | `https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{nm}` | 43,032 | 1 request/s, personal non-commercial use, citation with a link |
| OpenSky | `https://opensky-network.org/api/states/all?lamin=..&lomin=..&lamax=..&lomax=..` | 11,158 | Anonymous: 400 credits a day (one poll every 4 minutes); compact arrays; no aircraft type or route |
| airplanes.live | — | — | Withdrawn for non-feeders in 2026 |

Sizes scale with traffic: a small radius and a streaming reader, or OpenSky's compact
format. None gives origin or destination.

### Transit

- **TfL** (London), keyless at 50 requests a minute: tube status 13,988 B; arrivals at a
  stop 27,428 B, verbose, no field filter.
- **MTA** (New York), keyless: 72,649 B of GTFS-realtime protobuf per line group. Needs
  nanopb and a static stop table; a poor fit without real engineering.
- transport.rest (Berlin): 12,963 B of JSON, a community wrapper; its Deutsche Bahn
  sibling returned 503.
- Bike-share station feeds are close to 1 MB. Traffic and commute time: nothing keyless.
- A general transit widget is not achievable keyless.

### Sport

- **ESPN** `site.api.espn.com`: unofficial, undocumented, wide coverage, stable for
  years, and large: Premier League scoreboard 76 KB, NBA game day 126 KB, NFL 287 KB.
  It refused a User-Agent carrying an e-mail address and accepted plain ones.
- **TheSportsDB** free key `123`: 1.7 to 2.5 KB, but the free tier returns one event per
  query and no live scores; one key shared by every device is fragile.
- **NHL** `api-web.nhle.com` (unofficial): a team's scoreboard 17.7 KB, week schedule
  6.6 KB.
- **MLB** `statsapi.mlb.com`: has a `fields=` filter (712 B against 5,522 B); individual
  non-commercial use. The best-shaped sports API here.
- **Formula 1**: Jolpica (the Ergast successor), next race 825 B, standings 2,401 B,
  500 requests an hour. OpenF1's live data during sessions is paid.

## D. Other

| Source | Example | Bytes | Notes |
|---|---|---|---|
| Nager.Date holidays | `https://date.nager.at/api/v3/NextPublicHolidays/BR` | 2,399 | 100+ countries; `IsTodayPublicHoliday` answers by status code |
| Bluesky followers | `https://public.api.bsky.app/xrpc/app.bsky.actor.getProfile?actor=bsky.app` | 1,054 | Official public endpoint |
| Mastodon followers | `https://mastodon.social/api/v1/accounts/lookup?acct=Gargron` | 2,095 | Most instances |
| Lichess | `https://lichess.org/api/puzzle/daily` | 664 | Official; a chessboard suits 64x64 |
| chess.com | `https://api.chess.com/pub/puzzle` | 489 | Official published-data API |
| Steam players | `https://api.steampowered.com/ISteamUserStats/GetNumberOfCurrentPlayers/v1/?appid=730` | 48 | Keyless for this method |
| npm, PyPI downloads | `https://api.npmjs.org/downloads/point/last-week/express` | 83 | |
| Minecraft status | `https://api.mcstatus.io/v2/status/java/<host>` | 178 | |

YouTube and Twitch counts need keys.

**Daily museum art** is blocked by JPEG: the Art Institute of Chicago's image server
answered every request with a Cloudflare challenge; the Met's smallest image is a 221 KB
JPEG; Cleveland's 612 KB. Wikimedia Commons serves 120 px thumbnails (6 KB JPEG) and
would be the most reliable host once a JPEG decoder exists.

## Rejections at a glance

- Withdrawn, dead or keyed: airplanes.live, CoinCap, Stooq, Reuters RSS, Quotable,
  Numbers API, N2YO, football-data.org, YouTube, Twitch.
- Too big: MeteoAlarm, the aurora grid, bike-share feeds, GDACS, the Wikipedia feed
  without a streaming reader.
- Protobuf: MTA.
- Unofficial with a terms risk: Yahoo Finance, ESPN, NHL.
- JPEG only: APOD, EPIC, GOES, SDO, the museums.

## The twelve best

1. Open-Meteo air quality: 600 B, global AQI, PM2.5 and UV on a provider already used.
2. Open-Meteo marine: waves, sea temperature and the only global keyless tide curve.
3. USGS earthquake query: official, radius and magnitude filters, 475 B as text.
4. NOAA SWPC scales and Kp: 1 to 5 KB, an aurora and space-weather widget anywhere.
5. wheretheiss.at and a CelesTrak TLE: live position, and passes predicted on the device.
6. RainViewer: one 10 to 16 KB PNG becomes a 64x64 rain map with the decoder at hand.
7. Frankfurter: 111 B for any currency pair.
8. Coinbase spot, CoinGecko for breadth: 61 to 222 B.
9. mempool.space: block height, fees and price in 6 to 108 B.
10. Nager.Date: the next public holidays of 100+ countries.
11. Jolpica F1: the only cleanly keyless global sport source.
12. Lichess daily puzzle: 664 B and a natural 64x64 image.

Runners-up: Hacker News through Algolia, MLB for US owners, Bluesky and Mastodon
counters, NOAA tides for US owners, OpenSky at its anonymous rate.

# The competition: widgets on comparable displays

Researched 2026-10-03 from official documentation, repositories and product pages. A
claim without a primary page behind it is marked **unverified**; several findings came
from search-result summaries and are flagged where it matters. Part of
[the widget research](README.md).

## 1. Commercial pixel displays

### Tidbyt (Gen 1 / Gen 2), Pixlet, Tronbyt

- **Product and status.** A 64x32 HUB75 panel with an ESP32 in a wooden box; about
  100,000 units shipped ([Hackaday](https://hackaday.com/2025/03/29/open-source-framework-aims-to-keep-tidbyt-afloat/),
  [Modal](https://modal.com/blog/tidbyt-is-joining-modal)). Modal took the team on in
  November 2024; no new devices ship and the forum speaks of "radio silence" on
  community pull requests ([forum](https://discuss.tidbyt.com/t/rip-tidbyt/7048)). The
  cloud still runs (a search summary of status.tidbyt.com on 2026-09-30; the page itself
  was not opened).
- **Tronbyt.** Replacement firmware plus a self-hosted server, actively developed
  (v2.4.0 on 2026-09-13). The server is written in Go and drives Tidbyt Gen 1/2, Tronbyt
  S3, MatrixPortal S3, Waveshare S3, Raspberry Pi, and 64x64 panels since v2.4.0
  ([server](https://github.com/tronbyt/server), [firmware](https://github.com/tronbyt/firmware-esp32)).
- **Catalogue.** [tidbyt/community](https://github.com/tidbyt/community/tree/main/apps)
  holds 500+ apps (the listing fetched was truncated). Rough category counts: sports
  about 60, transit about 40, finance and crypto about 35, clocks about 25, weather and
  air about 20, developer and service status about 20; then countdowns, calendars, "of
  the day" content and generic bridges (apitext, apiimage, adafruitio).
- **Architecture.** The device only decodes images. It heartbeats to the cloud; the
  cloud picks the next app, runs its Starlark script and returns a WebP
  ([Pixlet docs](https://raw.githubusercontent.com/tidbyt/pixlet/main/docs/authoring_apps.md)).
  API responses are cached per app across all users. Keys are encrypted with
  `pixlet encrypt` and decrypted only in the cloud. Configuration is a declared schema
  (Text, Toggle, Dropdown, Color, Datetime, Location, Typeahead, Generated, OAuth2);
  OAuth redirects through `appauth.tidbyt.com`. Tronbyt keeps the model: the firmware
  polls `/next` and loops the WebP for the time in a `Tronbyt-Dwell-Secs` header.
- **Layout.** A widget tree (Row, Column, Marquee, Text, Image); on 64x32 the convention
  is a small logo plus two to four short text rows, a marquee for overflow.

### Divoom Pixoo 64 / Times Gate

- **Product.** Pixoo 64 is a 64x64 Wi-Fi LED frame; Times Gate has five 128x128 LCDs.
  Pixoo Max was not researched.
- **Catalogue.** Four channels (Faces, Cloud gallery, Visualizer, Custom). The "clock
  faces" are the widgets: a large community library with data-bound fields (YouTube
  subscribers, Twitch followers, US stocks, crypto, exchange rates, weather, world
  clocks, countdowns).
- **Architecture.** Cloud-centric. The device holds an MQTT session to Divoom's cloud
  and fetches weather, face data and gallery content from Divoom's servers
  ([notes](https://github.com/Grayda/pixoo_api/blob/main/NOTES.md)); whether a face's
  data is composited on the device or in the cloud is **unverified**. There is also an
  unauthenticated local HTTP API (`Channel/SetIndex`, `Draw/SendHttpGif`, text overlay),
  with about 40 frames per pushed animation. Community tools render on a PC and push
  frames.

### LaMetric Time / Sky

- **Product.** Time is 37x8 (an 8x8 colour icon plus 29x8 white).
- **Catalogue.** Clock and alarm, weather, radio, timer, stopwatch; top of the market:
  subscriber and follower counters, message board, date ticker, daily agenda, My Data
  DIY ([market](https://apps.lametric.com/)).
- **Architecture.** A declarative frame model. An indicator app is a list of frames
  (Name, Metric, Goal, Sparkline). Data arrives by push (HTTP POST with a token, LAN or
  cloud) or by poll: a URL that returns frames as JSON at a set interval
  ([guide](https://lametric-documentation.readthedocs.io/en/latest/guides/first-steps/first-lametric-indicator-app.html)).
  Whether the poll goes direct from the device or through LaMetric's cloud is
  **unverified**. OAuth and keys live on the developer's own web service.
- **Layout.** The canonical 8 px grammar: icon left, scrolling text right, optional goal
  bar or sparkline.

### Others

- **Ulanzi TC001** (8x32, ESP32, battery): stock firmware has time, weather, pomodoro,
  stopwatch, scoreboard, follower counts. Its real role is as the main AWTRIX 3 target.
- **iDotMatrix** (16/32/64 px): Bluetooth only; the phone app does clock, countdown,
  text, GIFs. Any data widget needs a helper that renders and pushes over BLE.
- **Govee Gaming Pixel Light** (2025; 32x32 and 52x32): scenes, AI art, dashboards for
  weather, NBA schedule, Bitcoin, countdowns. App and cloud driven; internals
  **unverified**.
- **Tuneshine** (64x64): a single-purpose album-art display for Spotify, Apple Music,
  Sonos and Last.fm. Art almost certainly comes through its cloud (**unverified**).
- **Glance LED**: a sports, stocks, crypto, weather and news ticker.

## 2. Open firmware projects

- **AWTRIX 3** (ESP32, 32x8). Built-in apps are only time, date, temperature, humidity
  and battery. Everything else is a custom app: JSON pushed over MQTT or HTTP
  ([API](https://raw.githubusercontent.com/Blueforcer/awtrix3/main/docs/api.md)) with
  keys such as `text`, `icon`, `duration`, `lifetime` (a stale app removes itself),
  `progress`, `bar`/`line` and `draw` primitives. The firmware fetches no internet data;
  Home Assistant, Node-RED or ioBroker hold the keys and the logic. The
  [flows hub](https://flows.blueforcer.de/) lists 305 flows: smart home 114, social 42,
  gaming 25, news 12. Apps rotate in a loop; notifications pre-empt.
- **Pixelix** (ESP32; 32x8 to 64x64). A slot rotation of on-device plugins
  ([repo](https://github.com/BlueAndi/Pixelix)): DateTime, IconText, Countdown, Sunrise,
  OpenMeteo, OpenWeather, ChicagoBusTracker, Volumio, GrabViaRest, GrabViaMqtt, sensors,
  DDP, SoundReactive, GameOfLife and a **Makapix plugin**. `GrabViaRest` is a
  user-configured URL-to-display plugin. The closest architectural sibling to p64.
- **Clockwise** (ESP32, 64x64 HUB75). Clock faces compiled in (Mario, Pacman, Words,
  World Map), plus Canvas: a JSON theme the device downloads, with text, datetime,
  base64 PNG sprites and shapes ([wiki](https://github.com/jnthas/clockwise/wiki/Canvas-Clockface)).
  Clock only, no live data.
- **ESP32-Trinity** (witnessmenow): Tetris clock, falling sand, games, spectrum display.
  His Spotify and YouTube Arduino libraries call those APIs from the ESP32 itself;
  SparkFun's album-art display uses the former with JPEGDEC on 64x64.
- **Morphing clocks**: NTP plus on-device weather.
- **Adafruit MatrixPortal** (CircuitPython): the device fetches JSON itself, keys in
  `settings.toml`. Guides: network clock, weather, moon phase clock, quote board,
  YouTube ON AIR sign, countdown, and the 2026
  [flight proximity tracker](https://learn.adafruit.com/matrixportal-s3-flight-proximity-tracker?view=all)
  (FlightAware AeroAPI).
- **rpi-rgb-led-matrix** (Raspberry Pi): the MLB, NHL, NFL and NBA LED scoreboards. The
  MLB board is a good model of a screen set: live game, pregame and postgame, standings,
  an idle clock/weather/news ticker.
- **WLED 2D**: effects and scrolling text; not a widget platform.
- **ESPHome**: an official HUB75 component; widgets are C++ lambdas in YAML drawing Home
  Assistant values. Data always comes from Home Assistant.
- **PixelIt**: a JSON API over REST and MQTT fed by Node-RED; the AWTRIX pattern.

## 3. Adjacent smart displays

- **TRMNL** (e-ink): the server renders an image; the device fetches it with a refresh
  rate. Plugins are Liquid templates over polled or webhook data; a self-hosted server
  is possible. By connections ([integrations](https://trmnl.com/integrations)): Private
  Plugin first, Weather second, Google Calendar fourth, Stock Price seventh.
- **Vestaboard**: a paid subscription with 450+ channels (Spotify, Google Calendar,
  Slack, sports, weather), plus cloud and local APIs.
- **MagicMirror²**: default modules are clock, calendar, weather, news feed and
  compliments; the most starred third-party ones are remote control, Google Photos,
  Todoist and traffic.
- **DAKboard**: calendar, photos, weather, news, to-dos, traffic, stocks.
- **Inkplate with Home Assistant**: Home Assistant renders a PNG, the device polls it.
- **Transit signs**: NYC Train Sign (Raspberry Pi and matrix), RideOnTime, Traintrackr;
  single-city, single-purpose, sold from $159 up.

## 4. Synthesis

### Widget types by frequency

| Rank | Widget type | Where it appears | Notes |
|---|---|---|---|
| 1 | Clock, date, themed faces | Everything | Always on-device |
| 2 | Weather | Nearly everything | Second on TRMNL |
| 3 | Countdown, timer, pomodoro, stopwatch | LaMetric, Ulanzi, Govee, Tidbyt, Pixelix, Adafruit, iDotMatrix | No network |
| 4 | Follower and subscriber counters | LaMetric, Ulanzi, Divoom, AWTRIX, Trinity | A staple of small tickers |
| 5 | Stocks, crypto, currency | Divoom, Govee, Tidbyt, TRMNL, Glance LED, DAKboard | |
| 6 | Sports scores and standings | Tidbyt's largest category, Pi scoreboards, Govee, Vestaboard | League-specific, high upkeep |
| 7 | Calendar, agenda, to-do | TRMNL, MagicMirror, DAKboard, LaMetric, Tidbyt, Vestaboard | OAuth or an ICS URL |
| 8 | Transit arrivals, bike share | Tidbyt's second category, Pixelix, dedicated signs | Per-city APIs |
| 9 | Now playing, album art | Tuneshine, Trinity, SparkFun, AWTRIX flows, Vestaboard | 64x64 suits album art |
| 10 | Home-automation values, "show my data" | AWTRIX, ESPHome, LaMetric, TRMNL, Pixelix | The most used plugin wherever it exists |
| 11 | Message board, notifications | LaMetric, AWTRIX, Vestaboard | |
| 12 | News, RSS, "of the day" | MagicMirror, MLB board, Tidbyt, AWTRIX | Weak below 64 px wide |
| 13 | Moon, sun, astronomy | Adafruit, Pixelix, Tidbyt | |
| 14 | Air quality, indoor sensors | Tidbyt, AWTRIX, Pixelix | |
| 15 | Flights overhead | Adafruit, flightportal, Tidbyt | Niche, well liked |
| 16 | Developer, CI and service status | Tidbyt | |
| 17 | Energy and fuel prices, 3D-printer status | AWTRIX flows, Tidbyt | |
| 18 | Games and generative art | Trinity, Pixelix, WLED | |

### Four architectural patterns

1. **Server renders, device shows an image.** Tidbyt/Tronbyt, TRMNL, Inkplate with Home
   Assistant, probably Tuneshine. Unlimited apps, secrets and OAuth off the device, no
   firmware update per app; a dead box without the server and no sub-second liveness.
2. **Dumb renderer with a declarative push API; a LAN hub owns the data.** AWTRIX 3,
   PixelIt, ESPHome, LaMetric local push, the Pixoo local API. Tiny firmware and endless
   integrations; nothing works out of the box without Home Assistant or Node-RED.
3. **Device polls a URL that returns display-ready data.** LaMetric poll apps, Pixelix
   GrabViaRest, Clockwise Canvas, Tidbyt's apitext. The device stays standalone and is
   extended by pointing it at any small JSON endpoint.
4. **Self-contained firmware: the device fetches and renders.** Pixelix plugins,
   Adafruit guides, Trinity projects, morphing clocks, the Pi scoreboards. No companion;
   each widget is firmware code, keys sit on the device, and a changed API breaks a
   widget until the next release (Adafruit's moon clock lost its data source that way).

p64 today is pattern 4, and the device-direct rule keeps it there; pattern 3 is
compatible with the rule.

### What runs entirely on a microcontroller, with direct HTTPS

| Widget | Example | API called from the device | Key |
|---|---|---|---|
| Weather | Pixelix OpenMeteo and OpenWeather plugins; Adafruit weather display | Open-Meteo; OpenWeatherMap | none; a user key |
| Sunrise, moon phase | Pixelix Sunrise; Adafruit moon clock | USNO, MET Norway (per search summaries) | none; computable locally, as p64 does |
| Flights overhead | Adafruit flight tracker; flightportal | FlightAware AeroAPI; FlightRadar24 unofficial | paid key; none but fragile |
| Transit | Pixelix ChicagoBusTracker | CTA Bus Tracker | agency key |
| Subscriber counter | arduino-youtube-api | YouTube Data API v3 | Google API key |
| Now playing and album art | spotify-api-arduino; SparkFun display | Spotify Web API plus a cover JPEG | OAuth refresh token on the device |
| Generic JSON value | Pixelix GrabViaRest | A user-supplied URL | the user's |
| Clock theme as JSON | Clockwise Canvas | raw.githubusercontent.com | none |

Sports, the largest Tidbyt category, has no microcontroller-only example in this survey.

### Not covered or thin

Pixoo Max, PxMatrix-era projects beyond the morphing clocks, the NHL and NFL scoreboard
internals and WLED's 2D documentation were not researched in depth. A product called
"Matrx" could not be found.

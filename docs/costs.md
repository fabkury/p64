# Build costs

[Home](../README.md) · [Build guide](build-your-own.md) · [Project status](status.md)

What one p64a and one p64b cost to build, from the maintainer's own invoices, with
parts, shipping and tax always shown separately. All amounts are US dollars. Prices are
the ones actually paid, on the dates given; they are a dated record, not a quote.
Section 3 is the log of every order behind the numbers.

## 1. One p64a

| Item | Parts | Shipping | Tax | Source |
|---|---:|---:|---:|---|
| ESP32-S3-RGB-Matrix controller | 24.99 | | | Waveshare, 2026-09-02 |
| RGB-Matrix-P2-64x64-B (GOB) panel | 31.99 | | | Waveshare, 2026-09-02 |
| PSU-27W-USB-C-C supply | 6.99 | | | Waveshare, 2026-09-02 |
| Waveshare freight (YunExpress, China to New York) | | 25.60 | | the three parts above shipped together |
| Shell v7a, JLC3DP FDM PLA | 10.23 | | | JLC3DP, 2026-09-28 |
| Cradle insert | 1.07 | | | JLC3DP, 2026-09-28 |
| JLC3DP shipping and payment fee, half of the two-shell order | | 6.24 | | see the note below |
| Two right-angle USB-C adapters | 4.99 | 0.00 | 0.44 | Amazon listing price, 2026-10-17 |
| M3×10 button-head hex screws, pack of 50 (a build uses six) | 5.99 | 0.00 | 0.53 | Amazon, 2026-09-18 |
| **Total** | **86.25** | **31.84** | **0.97** | **119.06 delivered** |

Not costed: a dab of glue or tape for the insert (the maintainer had it), a microSD
card (optional) and the USB data cable for the first flash.

## 2. One p64b

Everything in the p64a column, with the v7b shell in place of v7a, plus the encoder parts.

| Item | Parts | Shipping | Tax | Source |
|---|---:|---:|---:|---|
| Controller, panel and supply, as above | 63.97 | 25.60 | | Waveshare, 2026-09-02 |
| Shell v7b, JLC3DP FDM PLA | 10.10 | | | JLC3DP, 2026-09-28 |
| Cradle insert | 1.07 | | | JLC3DP, 2026-09-28 |
| JLC3DP shipping and payment fee, half of the two-shell order | | 6.24 | | see the note below |
| Two right-angle USB-C adapters | 4.99 | 0.00 | 0.44 | Amazon listing price, 2026-10-17 |
| M3×10 button-head hex screws, pack of 50 (a build uses six) | 5.99 | 0.00 | 0.53 | Amazon, 2026-09-18 |
| 2 × Adafruit 5880 encoder boards, $7.95 each | 15.90 | | 1.41 | Adafruit, 2026-09-11 |
| 2 × Adafruit 5528 silver 20 mm knobs, $2.95 each | 5.90 | | 0.52 | Adafruit, 2026-09-11 |
| Adafruit 4209 cable (SH to male headers, 150 mm) | 0.95 | | 0.08 | Adafruit, 2026-09-11 |
| Adafruit 4397 cable (SH to female sockets, 150 mm) | 0.95 | | 0.08 | Adafruit, 2026-09-11 |
| Adafruit 4401 cable (SH to SH, 200 mm, between the boards) | 1.25 | | 0.11 | Adafruit, 2026-09-27 |
| Adafruit shipping (USPS Ground Advantage, one order) | | 5.82 | 0.52 | the first Adafruit order; see the note below |
| 2.2 kΩ ½ W resistors, pack of 40 (a build uses two) | 3.99 | 0.00 | 0.35 | Amazon, 2026-09-17 |
| **Total** | **115.06** | **37.66** | **4.04** | **156.76 delivered** |

Optional, outside the total: a multi-colour heat-shrink pack (Adafruit 1649, $4.95) for
the spliced harness; electrical tape does the same job. Tools (soldering iron, meter,
hex key, breadboard for the bench test) are not build parts and are not counted; the
[encoder development record](hardware/encoder-history.md) lists the ones used.

Not costed, as for p64a: glue, microSD card, USB data cable.

## 3. How the numbers were split

- **Parts** are the invoice line prices. **Shipping** is what the carrier line of each
  invoice says. **Tax** is sales tax as charged: the two US vendors (Adafruit, Amazon)
  charged New York's 8.875 % on goods and on shipping; the two direct imports
  (Waveshare, JLC3DP) charged none. A builder elsewhere pays their own rate, or none.
- **JLC3DP shipping.** The v7 shells were printed in one order: one v7a shell, one v7b
  shell and three inserts, $23.54 of printing, $11.97 of shipping and a $0.50 payment
  fee. The tables charge each build half of the $12.47, or $6.24. A single shell with
  its insert shipped alone would cost more per build; the only single-shell data point
  is the v1 shell of 2026-09-05, $18.83 delivered.
- **Adafruit shipping.** The first Adafruit order (2026-09-11) carried every encoder
  part the build uses except the 200 mm 4401 cable, which came in the second order;
  the tables charge the first order's $5.82 to the build and none of the second
  order's $6.33, which mostly carried spares and the replacement board.
- **Adapters.** The build needs two right-angle USB-C adapters. For one p64 the
  maintainer recommends the [B0B2NJ3P3L listing](https://www.amazon.com/dp/B0B2NJ3P3L)
  at $4.99 (price seen 2026-10-17), which is what the tables carry, with tax at the
  New York rate. The maintainer's own adapters came as 4-packs at $6.99 each (two
  packs, 2026-10-10), the better buy for more than one unit.
- **Resistors and screws.** The smallest packs found were 40 resistors for $3.99 and
  50 screws for $5.99; a build uses two and six, and the tables carry the whole packs
  because a builder has to buy them.
- **Rounding.** Tax is computed per line at 8.875 % and rounded to the cent, so the
  column can differ by a cent from an invoice's total.

## 4. Order log

Every p64 purchase to date, oldest first, with what each order carried. Unrelated items
that shared an Amazon order are left out, and their share of that order's tax with them.

| Date | Vendor | Items | Parts | Shipping | Tax | Paid |
|---|---|---|---:|---:|---:|---:|
| 2026-09-02 | Waveshare | controller 24.99, GOB panel 31.99, 27 W supply 6.99 | 63.97 | 25.60 | 0.00 | 89.57 |
| 2026-09-05 | JLC3DP | v1 shell, black FDM PLA (received 2026-09-18); breakdown not recorded | | | | 18.83 |
| 2026-09-11 | Adafruit | 2 × 5880 boards 15.90, 2 × 5528 silver knobs 5.90, 4209 0.95, 4397 0.95, 4399 50 mm cable 0.95 | 24.65 | 5.82 | 2.70 | 33.17 |
| 2026-09-17 | Amazon | 2.2 kΩ resistors, 40-pack 3.99; three 400-point breadboards 5.98 (bench test only) | 9.97 | 0.00 | 0.88 | 10.85 |
| 2026-09-18 | Amazon | M3×10 button-head hex socket screws, black oxide stainless, 50-pack (free shipping) | 5.99 | 0.00 | 0.53 | 6.52 |
| 2026-09-28 | JLC3DP | v7a shell 10.23, v7b shell 10.10, 3 × cradle insert 3.21 (received 2026-10-09); payment fee 0.50 counted as shipping | 23.54 | 12.47 | 0.00 | 36.01 |
| 2026-09-27 | Adafruit | 3 × 5880 boards 23.85 (one replaced the board killed on the bench, two spare), 2 × 5529 blue knobs 5.90 (spare set), 2 × 4401 200 mm cables 2.50, 4209 0.95, 4397 0.95, heat-shrink pack 1649 4.95; received 2026-10-01 | 39.10 | 6.33 | 4.03 | 49.46 |
| 2026-10-10 | Amazon | 2 × 4-packs of right-angle USB-C adapters, 6.99 each (free shipping) | 13.98 | 0.00 | 1.24 | 15.22 |
| | **Total spent on p64 parts and prints** | | | | | **259.63** |

The total spent falls short of one p64a plus one p64b (275.82 by the tables, 119.06 +
156.76) only on paper: the tables price the adapters at the single-unit listing and the
resistor and screw packs in full in both builds, while the maintainer's spend includes spares, the replacement
board, the v1 shell and the breadboards, and excludes the tools bought for the
soldering ([encoder development record](hardware/encoder-history.md)).

## 5. Keeping this page true

Add a row to the order log for every purchase, on the day, with the invoice's own
split of parts, shipping and tax; recompute the per-build tables when a price in them
changes; never copy an order number, address or payment detail here. The
[build guide](build-your-own.md) and the [homepage](../README.md) quote only the totals
and link here.

# p64 documentation

The [repository README](../README.md) is p64's homepage.

| Audience | Start here | Details |
|---|---|---|
| Prospective builder | [Build guide](build-your-own.md) | Parts, printing, assembly, [flashing](flash-firmware.md), first setup |
| Owner | [Using p64](using-p64.md) | Artwork, playsets, widgets, controls, recovery |
| Contributor | [Firmware guide](../firmware/README.md) | [Testing](../firmware/docs/testing.md), [architecture](../firmware/docs/architecture.md), [releases](../firmware/docs/release-build.md) |
| Client developer | [API](../firmware/docs/api.md) | Authentication, routes, WebSocket and UDP |
| Enclosure designer | [Enclosure guide](../enclosure/README.md) | [Geometry and evidence](../enclosure/design-reference.md) |

## Product references

- [Status](status.md): assembly, implementation, release readiness, remaining checks and
  engineering notes.
- [Specification](spec/p64-spec.md): behavior, settings, limits and acceptance criteria.
- [Glossary](../CONTEXT.md): shared terminology.
- [Architecture decisions](adr/): reasons for consequential choices.
- [Encoder wiring](hardware/encoders-soldered.md): p64b's two-knob build.
- [Build costs](costs.md): what one p64a and one p64b cost, parts, shipping and tax
  apart, with the dated order log.
- [Credits](credits.md): licenses and asset sources.
- [Gallery](gallery.md): existing photos, clips and UI screenshots.

## History and research

These records describe dated work, not necessarily current instructions:

- [Firmware log](../firmware/docs/PROGRESS.md) and [decode benchmark](../firmware/docs/benchmarks.md).
- [Hardware bring-up](../firmware/reference/hardware-tests/README.md) and
  [September architecture review](review-2026-09/README.md).
- [Earlier enclosures](../enclosure/archive/pre-v7/README.md),
  [one-encoder experiment](hardware/encoders-solderless.md), and
  [encoder development record](hardware/encoder-history.md).
- [Widget research](../firmware/docs/widget-research/README.md),
  [clock design studies](../firmware/docs/design/clock-candidates/README.md),
  [video pipeline](video/README.md), [outreach log](outreach.md), and [session prompts](../prompt/).

## Maintaining the docs

[Illustration choices and regeneration](images/illustrations/README.md) cover visual
sources, placements and remaining photo opportunities.

Keep each fact in one main place. Other pages should summarize it and link here:

| Fact | Main document |
|---|---|
| Shopping list and assembly procedure | Build guide |
| Prices paid, shipping and tax, per-build totals | Build costs |
| Everyday operation and recovery | Owner's guide |
| Flashing a pre-built release over USB | Flashing guide (the web flasher's hosting: docs/web-flasher) |
| Release contents, how a release is cut, what the image carries for development | Release build (firmware/docs) |
| Implemented, tested, assembled or released | Status, linked to dated evidence |
| Product behavior, defaults and limits | Specification |
| Interface contract | API reference |
| Module ownership and runtime rules | Architecture |
| Dimensions, assumptions and measurements | Enclosure design reference and measurement records |

When behavior changes, update the specification and affected user/API instructions in
the same change. Add an ADR when reversing an architectural decision. Preserve dated
results as history; update status when evidence closes an outstanding check. Keep
homepage summaries brief and use relative links so they work on GitHub.

Before finishing, check local links and headings, commands and working directories,
settings against the implementation, and the path from the homepage to first artwork.
Review historical prices as dated examples rather than current quotes.

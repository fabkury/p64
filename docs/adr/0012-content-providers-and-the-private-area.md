---
status: accepted
date: 2026-09-24
---

# Channel sources behind one provider interface, and a private area outside the public repository

The show reaches every source of channel artworks that is not a folder on the card
through one interface, `content::Provider` (which channels it serves, what is playable
now, where an item's file is, what happened to the item), and a registry that providers
join at start-up. Makapix Club is the first provider; the show keeps no Makapix-specific
branch in its content path (only the pairing screens and the server-generated Followed
playset stay Makapix's). A new channel kind, `external`, addresses a provider's channels
as `<provider>:<channel>`, and the status document lists the registered providers and the
channels they offer, so the public web UI shows a provider it has never heard of. The
private area, `firmware/private/`, is a separate repository (`fabkury/p64-private`, never
published) mounted inside the public checkout and git-ignored there; the public build
discovers it when present (its components join the build, its `sdkconfig.private` applies,
the version carries `+private`, the host test runner compiles what its manifest names)
and is byte-for-byte the public firmware when it is absent, which is what CI and every
other clone build.

Why: the user wants a source of channels of their own, like Makapix Club but fully
private, without publishing a line of it and without forking the firmware. Before this the
show branched on "Makapix or local" in eight places (runtime, pick, load failure, shown
reports, install, change handler, channel JSON, history); a second source would have
meant a third branch in each, in the public tree, naming the private one. Making Makapix
the first client of the interface, rather than adding a generic path beside it, was chosen
so the seam is proven by the source that already works and the private one is the second
use, not the first. The cache, index and refresh machinery stays inside `p64_makapix` for
now: it is generalised on the second use, once the private provider's protocol shows
which shape a shared worker needs, not around Makapix's sqids and shards in advance.

Considered and rejected: a git submodule for the private area (records the private URL
and commit in the public repository and needs CI told to skip it); play-this and
transient playsets as the only private hook (no weights, no scheduler, no channel
history: not a channel); a private release channel for OTA (a token on the device for a
private repository, for one device); a private ignore rule in `.git/info/exclude` (the
user does not mind the folder's name being known, only its contents).

Two more hooks followed the same day, for providers that keep credentials: a provider may
declare a `settings_path` (its own page on the device; the public Settings page lists
every provider and links to that page, so a private provider gets a settings entry
without the public UI naming it) and `erase_credentials()`, which the factory reset calls
on every provider.

Consequences: a device running a private build must not install a public release without
knowing it drops the private parts; the OTA status carries `private_build` and the Update
page warns before the install. `firmware/budgets.json` and CI describe the public
firmware only: the private components' RAM and flash are measured on the device by hand.
No prompt about private work is recorded anywhere (`prompt/` holds public work only).
Playsets carrying an `external` channel of a provider that a build lacks keep the channel,
unusable, with the status "not supported yet". A provider's own presence and view
reporting is generic too: the provider of the item on the panel gets `note_shown`, every
other provider `note_hidden`.

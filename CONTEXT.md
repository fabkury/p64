# p64

p64 is a desktop 64x64 RGB LED matrix that plays pixel art: a Waveshare ESP32-S3 HUB75
driver board on a P2 64x64 panel in a printed shell. This glossary fixes the words the
product, its specification, its firmware and its web UI use. The specification is
`docs/spec/p64-spec.md`; decisions are in `docs/adr/`.

## Language

### Content

**Artwork**:
One playable file: an animation or a static image, from any source. The unit that
playback, history, channels and Makapix all count.
_Avoid_: post (Makapix's word for the server-side record), image, GIF (as a generic)

**Animation**:
An artwork with more than one frame (animated GIF, APNG, animated WebP). Always loops.

**Static image**:
An artwork with exactly one frame (GIF, WebP, PNG, BMP).

**Canvas**:
The artwork's own pixel dimensions before scaling to the panel.

**Frame delay**:
The time one animation frame stays on the panel, after the browser rule has been applied
to the value stored in the file.

**Background colour**:
The colour behind transparent pixels and in letterbox or pillarbox bars. Default black.

**Local file**:
An artwork stored on the microSD card by the user (upload, copy, or download from a URL).

### Sources and selection

**Channel**:
An ordered set of artworks from one source, identified by a kind and a name. A channel is
what a playset mixes.
_Avoid_: feed, folder (when meaning the channel), source (when meaning a specific channel)

**Channel kind**:
Where a channel's artworks come from: a local folder on the card, or a Makapix channel
(Promoted, All, Own, Artist, Hashtag, Reactions). Reserved kinds: URL list, pinned list.

**Local channel**:
A channel whose artworks are the files in one folder of the card's `animations/` tree.

**Makapix channel**:
A channel whose artworks are posts from Makapix Club, selected by the server-side channel
kinds (Promoted, All, Own, Artist by sqid, Hashtag, Reactions by sqid).

**Playset**:
A named mix of up to 64 weighted channels that the device plays from. The user has up to
32 of them; built-in playsets exist for each single source.
_Avoid_: playlist (Makapix's word for a curated post list), mix, queue

**Built-in playset**:
A playset the device provides without the user creating it: Promoted, All, Followed and
Local. It cannot be edited or deleted.

**Followed playset**:
The built-in playset the Makapix server generates for a paired device: one Artist channel
per artist the owner follows (server name `followed_artists`).

**Active playset**:
The playset the Animation show is currently drawing from. Restored at boot.

**Channel weight**:
The relative share of picks a channel gets within its playset. 0 mutes the channel; all
zero means equal shares.

**Channel selection**:
How the next channel is chosen in a playset: smooth weighted round-robin (SWRR) or
stochastic (weighted random). Default stochastic.

**Pick mode**:
How the next artwork is chosen within a channel: random, or recency (newest first with a
cursor). Default random. A global setting, not per playset.

**Channel offset**:
For ordered sources, the index the recency cursor starts from; wraps past the end.

**Channel index**:
The device's list of the artworks a channel contains (up to the per-channel cap), refreshed
on a schedule from the source. Not the files themselves.

**Artwork cache**:
The files of channel artworks downloaded to the card so playback never waits on the
network. Playback picks only among cached files.
_Avoid_: vault (Makapix's word for its own file store)

**Refresh**:
Bringing a channel index up to date with its source. Scheduled per channel.

**Last played**:
The moment a cached artwork's file was last read for the show, whatever asked for it
(a channel pick, history, play-this). A fresh download counts as played.

**Cache retention**:
How many days a cached artwork survives after it was last played. Older files are
deleted by the nightly cache sweep even when a channel of the active playset still lists
them; the channel downloads them again. Default 30, range 1 to 365, a user setting.

**Cache sweep**:
The deletion, once a night at the night schedule's start, of every cached artwork, URL
download and channel index older than the cache retention. Never touches the user's own
files. Runs only while the night schedule is enabled and the time is trusted.

**Trusted time**:
The wall clock once an NTP server has answered in the current boot; nothing else (no
RTC, no manual set) makes the time trusted (ADR 0011). Before it, clock features show
`--:--` and nothing that stores or compares dates runs.
_Avoid_: synced clock (for a time that came from anywhere but NTP)

### Playback

**Animation show**:
The main state: the device plays artworks from the active playset, swapping automatically.
_Avoid_: slideshow, rotation (that word means orientation here), gallery mode

**Auto-swap**:
The scheduled change to the next artwork after the auto-swap interval, a hard cut.

**Auto-swap interval**:
The number of seconds an artwork stays up before auto-swap. Default 30, range 5 to 86400,
0 means never.
_Avoid_: dwell, dwell time, slot length

**Swap**:
Any change of the artwork on the panel: auto-swap, next, previous, play-this, or an
interlude. Every swap is seamless.

**Seamless**:
The previous frame stays on the panel until the next content's first frame is fully
rendered, then the change lands on one panel refresh: no blank, no partial frame, no
flicker.

**History**:
The last 32 artworks (and interludes) shown, in order, with a position that previous and
next move through. Lives in memory only.

**Previous / Next**:
Manual swaps along history: previous walks back; next walks forward and, at the end of
history, picks a fresh artwork.

**Play-this**:
Playing one specific artwork now, outside the playset: a local file, a Makapix post, a URL
download, or a "send to device" command. It enters history.

**Pause**:
The panel goes dark and playback stops on the current artwork; resume restores it.
_Avoid_: freeze, hold

**Panel off**:
The night schedule's darkest setting: the panel shows nothing but the device keeps running.

### Widgets and interludes

**Widget**:
A full-screen view drawn by the device from data rather than from a file: Clock, Weather,
Temperature.
_Avoid_: app, screen, mode (for a widget)

**Widget state**:
The main state in which one chosen widget stays on the panel indefinitely.

**Interlude**:
A widget taking one auto-swap slot inside the Animation show, chosen by probability at an
auto-swap. Enters history like an artwork.

**Interlude probability**:
Per widget, the chance (0 to 100 %) that the widget wins the next auto-swap slot.

**Clock overlay**:
The small time display drawn in a corner over artworks in the Animation show. Not a
widget.

### Stream

**Stream**:
Pixels arriving over the network and shown directly, frame by frame, with the lowest
possible latency.

**Stream state**:
The main state in which the device waits for and shows a stream.

**Stream takeover**:
A stream interrupting the Animation show or a widget without changing the chosen state;
the interrupted state resumes after the silence timeout.

**Silence timeout**:
How long without stream frames before a stream is considered ended. Default 5 s.

### Panel and picture

**Panel**:
The 64x64 LED matrix, in its logical orientation after rotation.

**Panel mode**:
Quality mode (more tonal depth, lower refresh) or Photo mode (much higher refresh for
cameras, fewer tones).

**Brightness**:
The user's panel brightness, 1 to 255. Never a percentage.

**Brightness ceiling**:
An advanced upper bound (1 to 255) that no setting or schedule can exceed; protects rigs
on USB-only power.

**Night schedule**:
A daily time window during which brightness is lowered to a set value, or the panel is off.

**Rotation**:
The logical orientation of the picture on the panel: 0, 90, 180, 270 degrees clockwise, or
auto (from the IMU). Default 90 (the shell stands the panel that way).
_Avoid_: orientation (as a setting name)

**Scaling**:
Fitting a canvas into the panel: nearest-neighbour up, box-average down, aspect ratio kept,
bars in the background colour.

**Live preview**:
The web UI's copy of what the panel shows right now, taken from the device's own frame.

### Device and network

**Setup mode**:
The device running its own open Wi-Fi access point (`p64-setup`) with the setup portal
because it has no working Wi-Fi credentials.
_Avoid_: AP mode, captive mode, provisioning (that word is Makapix pairing)

**Setup portal**:
The streamlined web page served in setup mode for entering Wi-Fi credentials and the device
name.

**Device name**:
An optional short name (lowercase letters, digits, hyphens, up to 16) that becomes part of
the hostname (`p64-<name>`) and is shown in the web UI and on Makapix.

**Hostname**:
`p64` or `p64-<device name>`, reachable as `<hostname>.local`.

**PIN**:
An optional 4 to 8 digit code that gates the web UI and the HTTP API. Off by default.

**Status screen**:
Text the panel shows when the user must act or wait: setup, IP address, pairing code, no
artwork, stream waiting, update progress.

**Boot animation**:
The short procedural animation shown from power-on until the first artwork.

**Factory reset**:
Erasing settings, Wi-Fi credentials, PIN and Makapix pairing; the card is left alone.

### Makapix Club

**Makapix Club**:
The pixel-art community site (makapix.club) and its player API. "The server" in firmware
text.

**Post**:
Makapix's server-side record of an artwork, identified by a post id, a storage key and a
public sqid.

**Sqid**:
Makapix's short public identifier for a post or a user, as in `makapix.club/p/<sqid>`.

**Promoted**:
A post a Makapix moderator promoted; the Promoted channel is the only Makapix channel that
works without pairing.

**Pairing**:
Linking the device to a Makapix account: the device obtains a player key and shows a
6-character code, the owner enters it on the site, and the device receives its certificate
and token.
_Avoid_: provisioning (Makapix's internal name for the first step), registration (use only
for the server-side status)

**Player key**:
The device's permanent Makapix identity, issued at pairing.

**Send to device**:
A command from the Makapix site to a paired device: show this post, play this channel, play
this playset, and the panel settings the device advertises.

**View**:
The device telling Makapix which artwork it showed, so artists' statistics count devices.

**Like**:
A reaction the owner gives a post from the device's web UI, attributed to the owner.

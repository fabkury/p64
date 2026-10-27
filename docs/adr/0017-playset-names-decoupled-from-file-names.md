---
status: accepted
date: 2026-10-27
---

# Playset names are decoupled from their file names

A user playset name is any printable text of 1 to 32 code points, with no leading or
trailing space and no control characters (spec 5.2). Two names are the same playset when
they differ only in ASCII case or in the spacing between words (the canonical form:
trimmed, runs of spaces collapsed, ASCII letters lower-cased); the spelling stored on
the card is the playset's spelling, and a save under another spelling updates it rather
than renaming it. The name lives inside the playset's JSON file; the file's stem is
derived from it (`p64_content/src/playset_name.cpp`, host-tested):

- A name that fits the rule before this decision, 1 to 32 of `[A-Za-z0-9_]`, is stored
  as `channels/playsets/<name>.json`, exactly as before. For those files the stem stays
  the identity and the inner name is not consulted, so nothing on an existing card is
  read differently and nothing is rewritten.
- Any other name is stored as `<slug>~<hash>.json`: the ASCII letters and digits of the
  canonical form (other runs as `_`, at most 24 characters, `playset` when nothing is
  left), a tilde, and four hex digits of an FNV-1a hash of the canonical form. The file
  carries `"format": 2`. Such a file is trusted only when its inner name puts it exactly
  there; a file with the same stem but another canonical name is a hash collision, and a
  save into it is refused with the other name.

Why: the old rule existed because the name was the file name and a raw URL segment.
Readable names ("Rainy days", "Café") were wanted; p3a solved this with hashed file
names and the name inside the file. p64 keeps the readable legacy stems where it can,
for cards edited on a PC and for compatibility, and reserves the new naming for names the
old firmware could never have stored. The tilde is deliberate: the legacy validator
rejects it, so the firmware before this decision lists past a new-style file without
reading or touching it. A rollback therefore hides playsets with spaces or non-ASCII
letters, intact on the card, and the next upgrade shows them again; the active playset,
persisted by name in NVS, falls back to a built-in on the old firmware as a deleted
playset does. No migration runs at boot, so a power cut cannot leave a card half-moved.
The web routes percent-decode the name (`+` is not a space in a path), and the editor
normalises typed names to NFC and refuses look-alike spaces; the device folds only ASCII
case, since it has no Unicode tables.

Consequences: `valid_playset_name` is the new rule and `legacy_playset_name` the old
one (the shape of a legacy stem). The store resolves a legacy-shaped name against the
folder listing ignoring ASCII case, because FAT does. The Settings file manager shows
stems, not names, and renaming a new-style file there makes it unreadable (skipped at
debug level); accepted. Names reach the Makapix `play_playset` command and the history
as plain strings and are unaffected. `kMaxPlaysetName` counts code points, so a name is
up to 128 bytes; NVS, the history items and the 512-byte URI limit have room.

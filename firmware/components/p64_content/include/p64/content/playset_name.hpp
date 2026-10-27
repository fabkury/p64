// p64 -- the rules of user playset names (spec 5.2, ADR 0017). Pure, host-tested.
//
// A name is any printable text of 1 to 32 code points. Two names are the same playset
// when they differ only in ASCII case or in the spacing (canonical form). The name is
// stored inside the playset's JSON file; the file's stem is derived from it: a name
// that fits the legacy rule ([A-Za-z0-9_]{1,32}) keeps the legacy stem (the name
// itself, so older firmware still reads it), any other name gets an ASCII slug, a '~'
// and four hex digits of a hash of its canonical form. The '~' is what makes a
// new-style file invisible to the legacy rule, which is the rollback guard.
#pragma once

#include <cstdint>
#include <string>

namespace p64::content {

// The rule before ADR 0017: 1..32 of [A-Za-z0-9_]. Still the shape of a legacy stem.
bool legacy_playset_name(const std::string &name);
// The rule since ADR 0017: valid UTF-8, 1..32 code points, no control characters
// (below 0x20, 0x7F, U+0080..U+009F), no non-ASCII spaces or invisibles, no leading
// or trailing space.
bool valid_playset_name(const std::string &name);
// Trimmed, runs of spaces collapsed to one, ASCII letters lower-cased. Equality of
// canonical forms is identity of playsets.
std::string canonical_playset_name(const std::string &name);
bool same_playset_name(const std::string &a, const std::string &b);
// ASCII lower-case letters and digits of the canonical form, other runs as '_', at most
// 24 characters; "playset" when nothing is left.
std::string playset_slug(const std::string &name);
// FNV-1a 32 of the canonical form.
uint32_t playset_hash(const std::string &name);
// The file stem (without ".json") a name is stored under; see the header comment.
std::string playset_file_stem(const std::string &name);
// True for a stem written by the new layout (it carries the '~').
bool new_style_stem(const std::string &stem);
// Code points of a valid UTF-8 string (bytes that are not a lead byte do not count).
size_t utf8_length(const std::string &s);

}  // namespace p64::content

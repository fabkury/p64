// The Makapix server's document shapes, parsed without any transport (host-tested).
#pragma once

#include <string>

#include "cJSON.h"
#include "p64/content/makapix_index.hpp"

namespace p64::makapix::contract {

std::string str(const cJSON *obj, const char *key);
double num(const cJSON *obj, const char *key, double fallback = 0);

// Fills an entry from a post object of the feed or the RPC (false for non-artworks, a
// missing id, a storage key that is not a UUID, or an unknown format).
bool entry_from_post(const cJSON *post, content::MakapixEntry &out);

// Appends the artworks of one listing page (`list_key` "posts" or "items") and reads the
// cursor; `has_more` is false whenever the cursor is empty.
void fill_page(const cJSON *root, const char *list_key, content::MakapixEntries &out, std::string &next_cursor,
               bool &has_more);

// The file host URL of an entry: <scheme>://<host>/<shard>/<uuid>.<ext>.
std::string download_url(const content::MakapixEntry &e, const char *vault_host, bool tls);

}  // namespace p64::makapix::contract

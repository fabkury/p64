// The server's HTTP contract (reference/makapix/docs/player, docs/http-api/player-rpc.md):
// provisioning and credentials, the anonymous promoted feed, the player RPC over HTTPS
// with the bearer token (query_posts, get_playset, reactions), view events, certificate
// renewal, token rotation, and artwork downloads from the file host. Blocking; the
// worker task calls these one at a time.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cJSON.h"
#include "contract.hpp"
#include "credentials.hpp"
#include "p64/content/makapix_index.hpp"
#include "p64/content/playset.hpp"
#include "p64/makapix/makapix.hpp"
#include "p64/net/fetch.hpp"

namespace p64::makapix::api {

std::string base_url();  // https://<host>/api

struct Provision {
  std::string player_key, code, expires_at, mqtt_host, https_base;
  uint16_t mqtt_port = 0;
};
bool provision(Provision &out, std::string &error);
// 1 = credentials received, 0 = not registered yet (404), -1 = failure.
int credentials(const std::string &player_key, creds::Credentials &out, std::string &error);

// One page of a channel listing into `out` (appended), newest first.
// `session` (optional) keeps the connection open across pages.
bool promoted_page(const std::string &cursor, content::MakapixEntries &out, std::string &next_cursor, bool &has_more,
                   std::string &error, net::fetch::Session *session = nullptr);
// `max_side` goes to the server as width/height `lte` criteria (the promoted feed has no
// such filter; the fetcher drops oversized entries from both listings anyway).
bool query_page(const std::string &token, const ChannelRef &ref, uint16_t max_side, const std::string &cursor,
                content::MakapixEntries &out, std::string &next_cursor, bool &has_more, std::string &error,
                net::fetch::Session *session = nullptr);
// A single post by public sqid (anonymous).
bool post_by_sqid(const std::string &sqid, content::MakapixEntry &out, std::string &title, std::string &error);

using ViewEvent = contract::ViewEvent;
bool view(const std::string &token, const ViewEvent &v, std::string &error);
bool reaction(const std::string &token, int32_t post_id, bool add, std::string &error);
bool get_playset(const std::string &token, const std::string &name, content::Playset &out, std::string &error);
bool renew_cert(const std::string &token, creds::Credentials &io, uint32_t &expires_at, std::string &error);
bool rotate_token(const std::string &player_key, std::string &token, std::string &error);

std::string download_url(const content::MakapixEntry &e);
// Downloads a file (any URL) with an exact Content-Length check; 404 sets `missing`.
bool download(const std::string &url, std::vector<uint8_t> &out, size_t max_bytes, bool &missing, std::string &error,
              net::fetch::Session *session = nullptr);

// Fills an entry from a post object of the feed or the RPC (false for non-artworks).
bool entry_from_post(const cJSON *post, content::MakapixEntry &out);

}  // namespace p64::makapix::api

// p64 -- playsets as JSON: the API's shape (also the file format on the card).
//
//   {"name":"mix","channels":[
//      {"kind":"local","identifier":"pixel","display_name":"Pixel","weight":2,"offset":0},
//      {"kind":"promoted","weight":1}]}
//
// Parsing also accepts p3a's channel shape ("type":"sdcard"|"named"|"user"|..., with
// "name" as the sub-type) so playsets exported from a p3a can be pasted in.
#pragma once

#include <string>

#include "cJSON.h"
#include "p64/content/playset.hpp"

namespace p64::content {

// A new object the caller owns.
cJSON *playset_to_json(const Playset &playset);
// Fills `out` from an object; false with a reason when the shape is wrong. Does not
// validate ranges (call Playset::validate for that).
bool playset_from_json(const cJSON *json, Playset &out, std::string &error);

}  // namespace p64::content

// Shared includes of the host unit tests (doctest and every host-testable header).
#pragma once

#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "p64/decode/decoder.hpp"
#include "p64/gfx/frame.hpp"
#include "p64/gfx/scaler.hpp"
#include "p64/playback/frame_queue.hpp"
#include "cJSON.h"
#include "p64/content/history.hpp"
#include "p64/content/playset.hpp"
#include "p64/content/playset_json.hpp"
#include "p64/content/scheduler.hpp"
#include "p64/content/makapix_index.hpp"
#include "p64/gfx/text.hpp"
#include "p64/gfx/fonts.hpp"
#include "analogue.hpp"
#include "clock_format.hpp"
#include "weather_model.hpp"
#include "weather_icons.hpp"
#include "protocol.hpp"
#include "p64/system/night.hpp"
#include "p64/system/rtc_codec.hpp"
#include "tap.hpp"
#include "orientation.hpp"
#include "version.hpp"

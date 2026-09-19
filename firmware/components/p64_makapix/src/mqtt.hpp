// The persistent MQTT session over mutual TLS (reference/makapix/docs/mqtt-api):
// commands from the site, presence (status every 30 s, last will "offline"), the
// advertised capabilities and state (retained), view events and command acks.
#pragma once

#include <cstdint>
#include <string>

#include "api.hpp"

namespace p64::makapix::mqtt {

bool start();  // from the stored credentials; idempotent
void stop();
bool connected();
void publish_status(int32_t current_post_id);
void publish_state();
bool publish_view(const api::ViewEvent &v);
void publish_ack(const std::string &command_id, const char *status, const char *error);

}  // namespace p64::makapix::mqtt

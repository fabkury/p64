// p64 -- captive DNS for setup mode: answers every query with the access point's
// address so phones and laptops open the setup portal.
#pragma once

namespace p64::net::dns_hijack {

void start();  // idempotent
void stop();

}  // namespace p64::net::dns_hijack

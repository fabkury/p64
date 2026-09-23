# p64_net: Wi-Fi (station, setup mode with the captive portal), the one HTTP server the
# firmware runs, mDNS, SNTP, the time zone table, and the HTTP(S) fetch helper every
# outbound request uses. docs/architecture.md, spec 10 and 13. time_rules.cpp has no
# ESP-IDF includes (host-tested): the rules of trusted time, ADR 0011.
idf_component_register(
    SRCS
        "src/wifi.cpp"
        "src/http_server.cpp"
        "src/setup_portal.cpp"
        "src/dns_hijack.cpp"
        "src/clock.cpp"
        "src/time_rules.cpp"
        "src/tz.cpp"
        "src/fetch.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_system
        esp_http_server
        esp_http_client
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        espressif__mdns
        esp_timer
        json
        esp-tls
        mbedtls
        esp_app_format
)

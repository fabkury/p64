# p64_net: Wi-Fi (station, setup mode with the captive portal), the one HTTP server the
# firmware runs, mDNS, SNTP and the time zone table. docs/architecture.md, spec 10.
idf_component_register(
    SRCS
        "src/wifi.cpp"
        "src/http_server.cpp"
        "src/setup_portal.cpp"
        "src/dns_hijack.cpp"
        "src/clock.cpp"
        "src/tz.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_system
        esp_http_server
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        espressif__mdns
        esp_timer
        json
)

# p64_makapix: Makapix Club (spec 13): pairing and credentials, channel indexes and the
# artwork cache, downloads, MQTT commands and presence, views and likes. One worker task
# on core 0 owns every outbound HTTPS request. docs/architecture.md section 12.
idf_component_register(
    SRCS
        "src/makapix.cpp"
        "src/fetcher.cpp"
        "src/api.cpp"
        "src/cache.cpp"
        "src/credentials.cpp"
        "src/mqtt.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_content
    PRIV_REQUIRES
        p64_net
        p64_system
        p64_storage
        p64_decode
        p64_playback
        json
        nvs_flash
        mqtt
        mbedtls
        esp_timer
        esp_app_format
        log
)

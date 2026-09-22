# p64_ota: firmware updates from GitHub Releases (check, install with SHA256
# verification, rollback). version.cpp and release.cpp have no ESP-IDF includes (host-tested).
idf_component_register(
    SRCS
        "src/ota.cpp"
        "src/version.cpp"
        "src/release.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
    PRIV_REQUIRES
        p64_net
        p64_system
        app_update
        esp_app_format
        esp_partition
        esp_timer
        mbedtls
        log
)

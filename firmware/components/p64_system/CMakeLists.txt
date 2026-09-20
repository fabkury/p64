# p64_system: the settings document (NVS, JSON, typed), the event bus, the log ring
# buffer, small persisted state, reboot counters and the crash summary, the internal I2C
# bus and the RTC, the night schedule rule. docs/architecture.md section 7.
# night.cpp and rtc_codec.cpp have no ESP-IDF includes (host-tested).
idf_component_register(
    SRCS
        "src/settings.cpp"
        "src/event_bus.cpp"
        "src/log_ring.cpp"
        "src/state_store.cpp"
        "src/reliability.cpp"
        "src/i2c_bus.cpp"
        "src/rtc.cpp"
        "src/rtc_codec.cpp"
        "src/night.cpp"
        "src/flash_guard.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
        json
        esp_driver_i2c
    PRIV_REQUIRES
        nvs_flash
        esp_timer
        log
        heap
        espcoredump
        app_update
        esp_app_format
        esp_system
)

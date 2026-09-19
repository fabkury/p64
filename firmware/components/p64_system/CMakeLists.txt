# p64_system: the settings document (NVS, JSON, typed), the event bus, and later the log
# ring buffer, reboot counters and crash summary. docs/architecture.md section 7.
idf_component_register(
    SRCS
        "src/settings.cpp"
        "src/event_bus.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
    PRIV_REQUIRES
        nvs_flash
        json
        esp_timer
)

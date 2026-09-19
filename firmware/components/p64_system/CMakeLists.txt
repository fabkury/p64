# p64_system: the settings document (NVS, JSON, typed), the event bus, the log ring
# buffer, and later reboot counters and the crash summary. docs/architecture.md section 7.
idf_component_register(
    SRCS
        "src/settings.cpp"
        "src/event_bus.cpp"
        "src/log_ring.cpp"
        "src/state_store.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
    PRIV_REQUIRES
        nvs_flash
        json
        esp_timer
        log
        heap
)

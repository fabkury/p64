# p64_display: owns the HUB75 driver; frame pacing locked to the panel's DMA, rotation,
# per-channel gains, brightness and panel modes. See docs/architecture.md section 4.
idf_component_register(
    SRCS
        "src/display.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
    PRIV_REQUIRES
        esp-hub75
        esp_timer
        hal
        soc
)

# The private area (../private, see ../README.md "Private area") is not listed here:
# main.cpp calls p64::priv::start() under CONFIG_P64_PRIVATE, which the private
# component's Kconfig defines, and that component adds its include directory to main
# itself, so the dependency points from private to public and this file never changes.
idf_component_register(
    SRCS
        "main.cpp"
        "boot_animation.cpp"
        "show.cpp"
        "show_rules.cpp"
        "show_core.cpp"
        "loader.cpp"
        "status_screens.cpp"
        "ops.cpp"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        p64_gfx
        p64_display
        p64_decode
        p64_playback
        p64_storage
        p64_system
        p64_content
        p64_makapix
        p64_widgets
        p64_stream
        p64_inputs
        p64_ota
        esp_driver_gpio
        p64_net
        p64_web
        json
        esp_timer
        app_update
        esp_app_format
        nvs_flash
)

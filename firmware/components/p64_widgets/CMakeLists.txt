# p64_widgets: the clock, weather and temperature widgets and the clock overlay (spec 7,
# 6.1), the Open-Meteo fetcher, the SHTC3 sensor sampler, the weather icons.
# clock_format, weather_model and weather_icons_util have no ESP-IDF includes (host-tested).
idf_component_register(
    SRCS
        "src/widgets.cpp"
        "src/clock_format.cpp"
        "src/weather_model.cpp"
        "src/weather_icons.cpp"
        "src/weather_icons_util.cpp"
        "src/shtc3.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
        p64_playback
        p64_system
        json
    PRIV_REQUIRES
        p64_net
        p64_content
        esp_timer
        esp_driver_i2c
        log
)

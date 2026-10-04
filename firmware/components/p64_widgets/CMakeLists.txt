# p64_widgets: the clock, weather, temperature and air widgets and the clock overlay (spec 7,
# 6.1), the Open-Meteo fetcher, the SHTC3 sensor sampler, the weather icons.
# clock_format, analogue, faces, the themed faces (sprite, picture, solar, themed_common, face_*,
# clock_assets), face_air, air_model, weather_model and weather_icons_util have no ESP-IDF includes (host-tested).
idf_component_register(
    SRCS
        "src/widgets.cpp"
        "src/clock_format.cpp"
        "src/analogue.cpp"
        "src/faces.cpp"
        "src/sprite.cpp"
        "src/solar.cpp"
        "src/themed_common.cpp"
        "src/face_flip.cpp"
        "src/face_led.cpp"
        "src/face_nixie.cpp"
        "src/face_horizon.cpp"
        "src/face_words.cpp"
        "src/face_hourglass.cpp"
        "src/face_orrery.cpp"
        "src/face_horizon_rd.cpp"
        "src/face_aquarium.cpp"
        "src/picture.cpp"
        "src/clock_assets.cpp"
        "src/weather_model.cpp"
        "src/air_model.cpp"
        "src/face_air.cpp"
        "src/weather_icons.cpp"
        "src/weather_icons_util.cpp"
        "src/shtc3.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
        p64_decode
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

# p64_inputs: the IMU (QMI8658) sampler, tap gestures and gravity auto-rotation, and the
# p64b rotary encoders (two Adafruit seesaw boards on the external I2C bus). tap.cpp,
# orientation.cpp, encoder_model.cpp and seesaw_wire.hpp have no ESP-IDF includes
# (host-tested).
idf_component_register(
    SRCS
        "src/inputs.cpp"
        "src/qmi8658.cpp"
        "src/tap.cpp"
        "src/orientation.cpp"
        "src/encoder_model.cpp"
        "src/seesaw.cpp"
        "src/encoders.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
    PRIV_REQUIRES
        p64_system
        esp_timer
        esp_driver_i2c
        esp_driver_gpio
        log
)

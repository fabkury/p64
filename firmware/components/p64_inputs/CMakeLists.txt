# p64_inputs: the IMU (QMI8658) sampler, tap gestures and gravity auto-rotation; the
# encoder abstraction later. tap.cpp and orientation.cpp have no ESP-IDF includes
# (host-tested).
idf_component_register(
    SRCS
        "src/inputs.cpp"
        "src/qmi8658.cpp"
        "src/tap.cpp"
        "src/orientation.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
    PRIV_REQUIRES
        p64_system
        esp_timer
        esp_driver_i2c
        log
)

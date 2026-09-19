# p64_gfx: panel-sized RGB888 frames, rotation, scaling, drawing, PNG encoding. No
# ESP-IDF includes in the sources (host-testable); sdkconfig.h is only read for the
# panel geometry.
idf_component_register(
    SRCS
        "src/frame.cpp"
        "src/scaler.cpp"
        "src/png_encode.cpp"
    INCLUDE_DIRS
        "include"
)

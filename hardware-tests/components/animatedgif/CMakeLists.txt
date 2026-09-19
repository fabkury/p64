# bitbank2/AnimatedGIF as an ESP-IDF component. See README.md in this folder.
idf_component_register(
    SRCS "src/AnimatedGIF.cpp"
    INCLUDE_DIRS "src"
)

# __LINUX__: the library's "no Arduino" build (stdio instead of Arduino's File/millis;
# MAX_WIDTH 2048). PUBLIC because MAX_WIDTH sizes the GIFIMAGE struct that users embed.
# ALLOWS_UNALIGNED: the ESP32-S3 handles unaligned 32-bit loads from RAM; the Arduino
# build of this library sets it for ESP32 targets too.
target_compile_definitions(${COMPONENT_LIB} PUBLIC __LINUX__ ALLOWS_UNALIGNED)

# Vendored code: keep ESP-IDF's -Werror=all from failing the build on its warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)

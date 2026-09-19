# p64_decode: format sniffing and the artwork decoders behind one interface. No ESP-IDF
# includes (host-testable). GIF now; PNG/APNG, WebP and BMP arrive with milestone M2.
idf_component_register(
    SRCS
        "src/format.cpp"
        "src/gif_decoder.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
    PRIV_REQUIRES
        animatedgif
)

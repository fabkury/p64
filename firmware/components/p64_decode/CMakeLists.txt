# p64_decode: format sniffing and the artwork decoders behind one interface (GIF on
# AnimatedGIF, PNG/APNG on the APNG-patched libpng, WebP on libwebp, BMP hand-written).
# No ESP-IDF includes: the host tests build these sources with the PC's g++.
idf_component_register(
    SRCS
        "src/format.cpp"
        "src/gif_decoder.cpp"
        "src/png_decoder.cpp"
        "src/webp_decoder.cpp"
        "src/bmp_decoder.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
    PRIV_REQUIRES
        animatedgif
        libpng
        libwebp
)

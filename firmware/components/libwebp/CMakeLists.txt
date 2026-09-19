# libwebp 1.4.0, decoder side only (dec, dsp decode set, utils decode set, demux with
# WebPAnimDecoder), vendored from github.com/webmproject/libwebp tag v1.4.0. See
# P64-NOTES.md. No config.h: the library's defaults apply (no threads, no SIMD on Xtensa;
# the SIMD variants compile to nothing here and speed up the host tests on x86).
file(GLOB WEBP_DEC_SRCS   "${CMAKE_CURRENT_LIST_DIR}/libwebp/src/dec/*.c")
file(GLOB WEBP_DSP_SRCS   "${CMAKE_CURRENT_LIST_DIR}/libwebp/src/dsp/*.c")
file(GLOB WEBP_UTILS_SRCS "${CMAKE_CURRENT_LIST_DIR}/libwebp/src/utils/*.c")
file(GLOB WEBP_DEMUX_SRCS "${CMAKE_CURRENT_LIST_DIR}/libwebp/src/demux/*.c")

idf_component_register(
    SRCS ${WEBP_DEC_SRCS} ${WEBP_DSP_SRCS} ${WEBP_UTILS_SRCS} ${WEBP_DEMUX_SRCS}
    INCLUDE_DIRS "libwebp/src"
    PRIV_INCLUDE_DIRS "libwebp"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable -Wno-implicit-fallthrough)

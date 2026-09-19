# p64_stream: pixel streams over DDP (UDP 4048) and the raw p64 UDP format (4064):
# assembly by byte offset, conversion, scaling, the latest-frame source, start and
# silence events. protocol.cpp has no ESP-IDF includes (host-tested).
idf_component_register(
    SRCS
        "src/stream.cpp"
        "src/protocol.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
        p64_playback
        json
    PRIV_REQUIRES
        p64_system
        p64_content
        lwip
        esp_timer
        log
)

# p64_playback: Artwork (bytes + decoder + scaler), the ready-frame queue between the
# player and the renderer, the player task (decode ahead, timeline, no-drop rule) and the
# render task (the only presenter). docs/architecture.md sections 2 and 3.
idf_component_register(
    SRCS
        "src/artwork.cpp"
        "src/player.cpp"
        "src/renderer.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        p64_gfx
        p64_decode
        p64_display
    PRIV_REQUIRES
        esp_timer
)

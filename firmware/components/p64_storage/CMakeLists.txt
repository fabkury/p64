# p64_storage: the microSD card (SDMMC 1-bit, FAT), the layout under the card root, file
# reads for playback, listing for the file manager. Card I/O runs on core-0 tasks only.
idf_component_register(
    SRCS
        "src/card.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        fatfs
        sdmmc
        esp_driver_sdmmc
        vfs
)

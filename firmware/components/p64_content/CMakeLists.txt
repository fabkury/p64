# p64_content: the content model (spec section 5): channels and playsets, their JSON,
# the scheduler (channel selection and picks), the history, local folder indexes, the
# Makapix channel index format, the content provider interface and registry (ADR 0012)
# and the playset store on the card. playset, playset_name, scheduler, history,
# playset_json, makapix_index, provider, memory_window and device_playsets are host-tested (tests/host); local_index is POSIX only;
# playset_store needs the card.
idf_component_register(
    SRCS
        "src/playset.cpp"
        "src/playset_name.cpp"
        "src/playset_json.cpp"
        "src/scheduler.cpp"
        "src/history.cpp"
        "src/local_index.cpp"
        "src/makapix_index.cpp"
        "src/provider.cpp"
        "src/memory_window.cpp"
        "src/device_playsets.cpp"
        "src/playset_store.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
        p64_storage
        p64_system
    PRIV_REQUIRES
        log
)

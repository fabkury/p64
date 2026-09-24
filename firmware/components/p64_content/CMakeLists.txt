# p64_content: the content model (spec section 5): channels and playsets, their JSON,
# the scheduler (channel selection and picks), the history, local folder indexes, the
# Makapix channel index format, the content provider interface and registry (ADR 0012)
# and the playset store on the card. playset, scheduler, history, playset_json,
# makapix_index and provider are host-tested (tests/host); local_index is POSIX only;
# playset_store needs the card.
idf_component_register(
    SRCS
        "src/playset.cpp"
        "src/playset_json.cpp"
        "src/scheduler.cpp"
        "src/history.cpp"
        "src/local_index.cpp"
        "src/makapix_index.cpp"
        "src/provider.cpp"
        "src/playset_store.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
        p64_storage
    PRIV_REQUIRES
        log
)

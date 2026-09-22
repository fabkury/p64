cmake_minimum_required(VERSION 3.16)

# p64 firmware. Behaviour: ../docs/spec/p64-spec.md. Structure: docs/architecture.md.
#
# sdkconfig.defaults is the source of truth for the configuration; the generated
# sdkconfig is git-ignored. sdkconfig.secrets (git-ignored, see sdkconfig.secrets.example)
# may seed development Wi-Fi credentials on top of the defaults.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.secrets")
    message(STATUS "p64: applying sdkconfig.secrets (development Wi-Fi seed)")
endif()

# Firmware version, embedded in the image and compared by OTA. MAJOR.MINOR.PATCH[-suffix].
set(PROJECT_VER "0.1.0-dev")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p64)

# Warnings in p64's own code are errors (review of 2026-09-22, proposal P-T4). ESP-IDF
# builds everything with -Werror=all but relaxes unused-*, deprecated-declarations and
# -Wextra; those relaxations stay for vendored code (esp-hub75, animatedgif, libpng,
# libwebp) and are withdrawn here for the p64 components and main.
set(P64_STRICT_WARNINGS
    -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations)
foreach(comp main p64_gfx p64_decode p64_display p64_system p64_playback p64_storage p64_content
             p64_net p64_web p64_makapix p64_widgets p64_stream p64_inputs p64_ota)
    idf_component_get_property(lib ${comp} COMPONENT_LIB)
    target_compile_options(${lib} PRIVATE ${P64_STRICT_WARNINGS})
endforeach()

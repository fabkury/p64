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

# The private area (private/, a separate repository mounted there, git-ignored; see
# README.md "Private area"): when present, its components join the build, its
# sdkconfig.private is applied last, and the version carries "+private" so a private
# build is recognisable (the Update page warns before a public release replaces it).
# Absent (CI, other clones), the build is the public firmware and nothing here runs.
# -DP64_NO_PRIVATE=ON builds the public firmware with the folder in place (the CI parity
# check: `tools/idf.ps1 -B build_public -DP64_NO_PRIVATE=ON build`).
option(P64_NO_PRIVATE "Build the public firmware even when private/ is present" OFF)
set(P64_PRIVATE_DIR "${CMAKE_CURRENT_LIST_DIR}/private")
set(P64_PRIVATE OFF)
if(EXISTS "${P64_PRIVATE_DIR}/components" AND NOT P64_NO_PRIVATE)
    set(P64_PRIVATE ON)
    list(APPEND EXTRA_COMPONENT_DIRS "${P64_PRIVATE_DIR}/components")
    if(EXISTS "${P64_PRIVATE_DIR}/sdkconfig.private")
        list(APPEND SDKCONFIG_DEFAULTS "private/sdkconfig.private")
    endif()
    message(STATUS "p64: private area present (${P64_PRIVATE_DIR})")
endif()

# Firmware version, embedded in the image and compared by OTA. MAJOR.MINOR.PATCH[-suffix].
set(PROJECT_VER "0.1.0-dev")
if(P64_PRIVATE)
    string(APPEND PROJECT_VER "+private")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p64)

# Warnings in p64's own code are errors (review of 2026-09-22, proposal P-T4). ESP-IDF
# builds everything with -Werror=all but relaxes unused-*, deprecated-declarations and
# -Wextra; those relaxations stay for vendored code (esp-hub75, animatedgif, libpng,
# libwebp) and are withdrawn here for the p64 components and main.
set(P64_STRICT_WARNINGS
    -Werror=extra -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations)
set(P64_STRICT_COMPONENTS main p64_gfx p64_decode p64_display p64_system p64_playback p64_storage p64_content
                          p64_net p64_web p64_makapix p64_widgets p64_stream p64_inputs p64_ota)
if(P64_PRIVATE)
    # The private components are held to the same standard.
    file(GLOB P64_PRIVATE_COMPONENTS LIST_DIRECTORIES true RELATIVE "${P64_PRIVATE_DIR}/components"
         "${P64_PRIVATE_DIR}/components/*")
    foreach(comp ${P64_PRIVATE_COMPONENTS})
        if(EXISTS "${P64_PRIVATE_DIR}/components/${comp}/CMakeLists.txt")
            list(APPEND P64_STRICT_COMPONENTS ${comp})
        endif()
    endforeach()
endif()
foreach(comp ${P64_STRICT_COMPONENTS})
    idf_component_get_property(lib ${comp} COMPONENT_LIB)
    target_compile_options(${lib} PRIVATE ${P64_STRICT_WARNINGS})
endforeach()

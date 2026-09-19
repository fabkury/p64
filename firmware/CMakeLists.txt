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

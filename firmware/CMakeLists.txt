cmake_minimum_required(VERSION 3.16)

# sdkconfig.secrets (git-ignored, see sdkconfig.secrets.example) holds the Wi-Fi
# credentials and is applied on top of the committed defaults when present.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.secrets")
    message(STATUS "p64: applying sdkconfig.secrets")
else()
    message(STATUS "p64: no sdkconfig.secrets, Wi-Fi credentials left empty")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(p64)

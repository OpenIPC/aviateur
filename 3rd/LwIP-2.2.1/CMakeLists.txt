cmake_minimum_required(VERSION 3.18)
project(lwip C)

set(LWIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE LWIP_SOURCES
        "${LWIP_ROOT}/src/core/*.c"
        "${LWIP_ROOT}/src/core/ipv4/*.c"
)

add_library(lwip STATIC ${LWIP_SOURCES}
        src/lwipopts.h)

target_include_directories(lwip PUBLIC
        "${LWIP_ROOT}/src"
)

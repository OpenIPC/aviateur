# Common sources for all platforms
file(GLOB WIFI_COMMON_SRC_LIST
        *.cpp
        wfb-ng/rx.cpp
        wfb-ng/wifibroadcast.cpp
        wfb-ng/zfex.c
)

target_sources(${PROJECT_NAME} PRIVATE ${WIFI_COMMON_SRC_LIST})

if (WIN32)
    # Windows specific
    if (MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /FI"net_compat.h")
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/net_compat.h")
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock)

    # We still need endian.h for some files
    target_include_directories(${PROJECT_NAME} PRIVATE cross compat)
else ()
    # Linux/macOS specific
    file(GLOB_RECURSE PLATFORM_SRC_LIST
            linux/*.cpp
    )
    target_sources(${PROJECT_NAME} PRIVATE ${PLATFORM_SRC_LIST})
endif ()

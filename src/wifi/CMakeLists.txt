file(GLOB WIFI_SRC_LIST
        *.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${WIFI_SRC_LIST})

if (WIN32)
    file(GLOB_RECURSE PLATFORM_SRC_LIST
            cross/*.cpp
            wfb-ng/zfex.c
    )
else ()
    file(GLOB_RECURSE PLATFORM_SRC_LIST
            wfb-ng/*.cpp
            wfb-ng/*.c
            linux/*.cpp
            cross/endian.h
    )
endif ()

target_sources(${PROJECT_NAME} PRIVATE ${PLATFORM_SRC_LIST})

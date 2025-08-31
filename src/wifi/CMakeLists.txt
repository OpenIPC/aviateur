file(GLOB WIFI_SRC_LIST
        *.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${WIFI_SRC_LIST})

if (WIN32 OR APPLE)
    file(GLOB_RECURSE WINDOWS_SRC_LIST
            windows/*.cpp
            wfb-ng/zfex.c
    )

    target_sources(${PROJECT_NAME} PRIVATE ${WINDOWS_SRC_LIST})
else ()
    file(GLOB_RECURSE LINUX_SRC_LIST
            wfb-ng/*.cpp
            wfb-ng/*.c
            linux/*.cpp
    )

    target_sources(${PROJECT_NAME} PRIVATE ${LINUX_SRC_LIST})
endif ()

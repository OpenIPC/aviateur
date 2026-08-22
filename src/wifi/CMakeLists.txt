file(GLOB WIFI_SRC_LIST
        *.cpp
)

file(GLOB WFB_SRC_LIST
        wfb-ng/*.c
        wfb-ng/*.cpp
)

if (WIN32)
else ()
    file(GLOB PLATFORM_SRC_LIST
            linux/*.cpp
    )
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/compat)

target_sources(${PROJECT_NAME} PRIVATE ${WIFI_SRC_LIST} ${WFB_SRC_LIST} ${PLATFORM_SRC_LIST})

if (AVIATEUR_ENABLE_OPENCV)
    file(GLOB FEATURE_SRC_LIST
            *.cpp
            *.c
            *.h
    )

    target_sources(${PROJECT_NAME} PRIVATE ${FEATURE_SRC_LIST})
endif ()

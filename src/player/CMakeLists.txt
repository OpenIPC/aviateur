file(GLOB_RECURSE PLAYER_SRC_LIST
        *.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${PLAYER_SRC_LIST})

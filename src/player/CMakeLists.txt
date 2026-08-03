file(GLOB_RECURSE PLAYER_SRC_LIST
        *.cpp
)

# On Apple platforms, yuv_renderer.cpp needs Objective-C++ compilation for Metal/CoreVideo.
if(APPLE)
    list(FILTER PLAYER_SRC_LIST EXCLUDE REGEX ".*/yuv_renderer\\.cpp$")
endif()

target_sources(${PROJECT_NAME} PRIVATE ${PLAYER_SRC_LIST})

if(APPLE)
    # Use a configure_file trick: copy yuv_renderer.cpp to yuv_renderer_objcpp.mm
    # in the build directory at configure time. The .mm extension makes CMake
    # natively classify it as OBJCXX, so all OBJCXX compile options/flags apply
    # automatically. This avoids maintaining two source files.
    set(_src ${CMAKE_CURRENT_SOURCE_DIR}/yuv_renderer.cpp)
    set(_dst ${CMAKE_CURRENT_BINARY_DIR}/yuv_renderer_objcpp.mm)
    configure_file(${_src} ${_dst} COPYONLY)
    target_sources(${PROJECT_NAME} PRIVATE ${_dst})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
endif()

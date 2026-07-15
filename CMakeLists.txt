cmake_minimum_required(VERSION 3.18)

# This should go before project declaration.
if (WIN32)
    message("[Aviateur] Using vcpkg in <$ENV{VCPKG_ROOT}>, which should be the vcpkg dir in which you have installed the dependency libs.")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif ()

project(aviateur
        VERSION 0.1
        LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJC OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")
endif ()

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(AVIATEUR_ENABLE_OPENCV "Enable OpenCV" OFF)

find_package(PkgConfig REQUIRED)

if (WIN32)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

    find_package(FFMPEG REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
else ()
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
            libavformat
            libavcodec
            libswresample
            libswscale
            libavutil
    )

    pkg_check_modules(LIBSODIUM REQUIRED IMPORTED_TARGET libsodium)
endif ()

if (AVIATEUR_ENABLE_OPENCV)
    find_package(OpenCV REQUIRED)
endif ()

file(COPY assets DESTINATION ${CMAKE_BINARY_DIR}/bin)

if (APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
            src/main.cpp
            src/gui_interface.h
    )
else ()
    add_executable(${PROJECT_NAME}
            src/main.cpp
            src/gui_interface.h
    )
endif ()

if (AVIATEUR_ENABLE_OPENCV)
    target_compile_definitions(
            ${PROJECT_NAME} PRIVATE
            AVIATEUR_USE_OPENCV
    )
endif ()

# Enable SIMD FEC
target_compile_definitions(${PROJECT_NAME} PRIVATE
        ZFEX_UNROLL_ADDMUL_SIMD=8
        ZFEX_USE_INTEL_SSSE3
        ZFEX_USE_ARM_NEON
        ZFEX_INLINE_ADDMUL
        ZFEX_INLINE_ADDMUL_SIMD
)

add_subdirectory(src/gui)
add_subdirectory(src/player)
add_subdirectory(src/wifi)
add_subdirectory(src/feature)

if (WIN32)
    string(APPEND CMAKE_CXX_FLAGS " /utf-8")
endif ()

add_subdirectory(3rd/devourer)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/devourer/src" "3rd/devourer/hal")

add_subdirectory(3rd/vecgui)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/vecgui/src")

add_subdirectory(3rd/json)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/json/include")

add_subdirectory(3rd/mINI)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/mINI/src")

add_subdirectory(3rd/SDL)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/SDL/include")

target_include_directories(${PROJECT_NAME} PRIVATE "src/wifi/wfb-ng/include")

file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

if (APPLE)
    # Do this after we copied assets for both Aviateur & VecGui.
    file(COPY ${CMAKE_BINARY_DIR}/bin/assets
            DESTINATION ${CMAKE_BINARY_DIR}/bin/aviateur.app/Contents/Resources
    )
endif ()

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${FFMPEG_LIBRARIES}
            PkgConfig::LIBUSB
            unofficial-sodium::sodium
            WiFiDriver
            vecgui
            SDL3::SDL3-static
    )
else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE
            PkgConfig::LIBAV
            PkgConfig::LIBSODIUM
            WiFiDriver
            vecgui
            SDL3::SDL3-static
            pcap
    )
endif ()

if (AVIATEUR_ENABLE_OPENCV)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${OpenCV_LIBS}
    )
endif ()

# Deployment
if (APPLE)
    set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/assets/Info.plist.in")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.aviateur.app")
    set(MACOSX_BUNDLE_BUNDLE_NAME "Aviateur")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_ICON_FILE "logo.icns")

    set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${MACOSX_BUNDLE_INFO_PLIST}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${MACOSX_BUNDLE_GUI_IDENTIFIER}"
            MACOSX_BUNDLE_BUNDLE_NAME "${MACOSX_BUNDLE_BUNDLE_NAME}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${MACOSX_BUNDLE_BUNDLE_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${MACOSX_BUNDLE_SHORT_VERSION_STRING}"
            MACOSX_BUNDLE_ICON_FILE "${MACOSX_BUNDLE_ICON_FILE}"
    )

    install(TARGETS ${PROJECT_NAME}
            BUNDLE DESTINATION .
            COMPONENT Runtime
    )

    install(FILES "${CMAKE_SOURCE_DIR}/assets/logo.icns"
            DESTINATION "aviateur.app/Contents/Resources"
            COMPONENT Runtime
    )

    include(InstallRequiredSystemLibraries)
    include(CPack)

    set(CPACK_PACKAGE_NAME "Aviateur")
    set(CPACK_PACKAGE_VENDOR "Aviateur")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Aviateur - OpenIPC FPV Ground Station")
    set(CPACK_PACKAGE_FILE_NAME "Aviateur-${PROJECT_VERSION}-macOS")
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Aviateur")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_SOURCE_DIR}/assets/dmg-setup.scpt")
endif ()

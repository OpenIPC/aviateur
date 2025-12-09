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
    enable_language(OBJC)
endif ()

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(AVIATEUR_ENABLE_GSTREAMER "Enable gstreamer" OFF)

find_package(PkgConfig REQUIRED)

if (WIN32)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

    find_package(FFmpeg REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    find_package(OpenCV REQUIRED)

    if (AVIATEUR_ENABLE_GSTREAMER)
        # To ensure dll linking, add "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin" to your PATH
        set(GST_ROOT "C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64")

        set(GST_LIB_ROOT "${GST_ROOT}\\lib")

        set(GST_INCLUDE_DIRS "${GST_ROOT}\\include\\gstreamer-1.0")
        set(GST_LIBRARIES "${GST_LIB_ROOT}\\gstreamer-1.0.lib" "${GST_LIB_ROOT}\\gstapp-1.0.lib" "${GST_LIB_ROOT}\\gstvideo-1.0.lib")

        set(GLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\glib-2.0" "${GST_LIB_ROOT}\\glib-2.0\\include")
        set(GLIB_LIBRARIES "${GST_LIB_ROOT}\\gobject-2.0.lib" "${GST_LIB_ROOT}\\glib-2.0.lib")

        set(GIO_INCLUDE_DIRS "${GST_ROOT}\\include\\gio-win32-2.0")
        set(GIO_LIBRARIES "${GST_LIB_ROOT}\\gio-2.0.lib")
    endif ()
elseif (ANDROID)

else ()
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
            libavformat
            libavcodec
            libswresample
            libswscale
            libavutil
    )

    pkg_check_modules(LIBSODIUM REQUIRED IMPORTED_TARGET libsodium)

    find_package(OpenCV REQUIRED)

    if (AVIATEUR_ENABLE_GSTREAMER)
        if (APPLE)
            find_library(GST_FRAMEWORK GStreamer
                    PATHS /System/Library/Frameworks/
                    /Library/Frameworks/
                    macOS_BUNDLE_DIRECTORY
            )
        else ()
            pkg_check_modules(GLIB REQUIRED glib-2.0)
            pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-plugins-base-1.0 gstreamer-plugins-bad-1.0 gstreamer-video-1.0)
            pkg_check_modules(GIO REQUIRED gio-2.0)
        endif ()
    endif ()
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

if (AVIATEUR_ENABLE_GSTREAMER)
    target_compile_definitions(
            ${PROJECT_NAME} PRIVATE
            AVIATEUR_USE_GSTREAMER
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

add_subdirectory(3rd/revector)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/revector/src")

add_subdirectory(3rd/json)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/json/include")

add_subdirectory(3rd/mINI)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/mINI/src")

add_subdirectory(3rd/SDL)
target_include_directories(${PROJECT_NAME} PRIVATE "3rd/SDL/include")

target_include_directories(${PROJECT_NAME} PRIVATE "src/wifi/wfb-ng/include")

file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

if (APPLE)
    # Do this after we copied assets for both Aviateur & Revector.
    file(COPY ${CMAKE_BINARY_DIR}/bin/assets
            DESTINATION ${CMAKE_BINARY_DIR}/bin/aviateur.app/Contents/Resources
    )
endif ()

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${FFMPEG_LIBRARIES}
            PkgConfig::LIBUSB
            unofficial-sodium::sodium
            ${OpenCV_LIBS}
            WiFiDriver
            revector
            SDL3::SDL3-static
    )
else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE
            PkgConfig::LIBAV
            PkgConfig::LIBSODIUM
            ${OpenCV_LIBS}
            WiFiDriver
            revector
            SDL3::SDL3-static
            pcap
    )
endif ()

if (AVIATEUR_ENABLE_GSTREAMER)
    if (APPLE)
        target_link_libraries(
                ${PROJECT_NAME} PRIVATE
                ${GST_FRAMEWORK}
        )
    else ()
        target_link_libraries(
                ${PROJECT_NAME} PRIVATE
                ${GST_LIBRARIES}
                ${GLIB_LIBRARIES}
                ${GIO_LIBRARIES}
        )

        target_include_directories(
                ${PROJECT_NAME} PRIVATE
                ${GST_INCLUDE_DIRS}
                ${GLIB_INCLUDE_DIRS}
                ${GIO_INCLUDE_DIRS}
        )
    endif ()
endif ()

if (APPLE)
    ##install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/aviateur.app"
    ##        DESTINATION "${CMAKE_BINARY_DIR}")
    #install(TARGETS aviateur DESTINATION "${CMAKE_BINARY_DIR}")
    ## Include CPack
    #include(CPack)
    #
    ## Set package information
    #set(CPACK_PACKAGE_NAME "Aviateur")
    #set(CPACK_PACKAGE_VERSION "0.1.15")
    #set(CPACK_PACKAGE_VENDOR "OpenIPC")
    #set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A brief description of your app.")
    #
    ## Specify the DragNDrop generator for macOS DMG
    #set(CPACK_GENERATOR "DragNDrop")
    #
    ## Configure DMG specific options (optional)
    ##set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/background.png") # Path to a custom background image
    #set(CPACK_DMG_APPLICATIONS_SYMLINK_LAUNCH_COMMAND "open /Applications/${CPACK_PACKAGE_NAME}.app") # Command to launch your app
    ##set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/setup_ds_store.applescript") # Custom AppleScript for .DS_Store layout
    #
endif ()

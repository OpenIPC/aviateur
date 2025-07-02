cmake_minimum_required(VERSION 3.20)

# This should go before project declaration.
if (WIN32)
    message("[aviateur] Using vcpkg in <$ENV{VCPKG_ROOT}>, which should be the vcpkg dir in which you have installed the dependency libs.")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif ()

project(aviateur
        VERSION 0.1
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(AVIATEUR_ENABLE_GSTREAMER "Enable gstreamer" ON)

find_package(PkgConfig REQUIRED)

if (WIN32)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

    find_package(FFmpeg REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    find_package(OpenCV REQUIRED)

    if (AVIATEUR_ENABLE_GSTREAMER)
        # To ensure dll linking, add "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin" to your PATH
        set(GST_ROOT "C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64")
        # For older versions of gstreamer
        #    set(GST_ROOT "C:\\gstreamer\\1.0\\msvc_x86_64")

        set(GST_LIB_ROOT "${GST_ROOT}\\lib")

        set(GST_INCLUDE_DIRS "${GST_ROOT}\\include\\gstreamer-1.0" "${GST_LIB_ROOT}\\gstreamer-1.0\\include")
        set(GST_LIBRARIES "${GST_LIB_ROOT}\\gstreamer-1.0.lib")
        set(GST_SDP_LIBRARIES "${GST_LIB_ROOT}\\gstsdp-1.0.lib")
        set(GST_WEBRTC_LIBRARIES "${GST_LIB_ROOT}\\gstwebrtc-1.0.lib")
        set(GST_GL_LIBRARIES "${GST_LIB_ROOT}\\gstgl-1.0.lib")
        set(GST_APP_LIBRARIES "${GST_LIB_ROOT}\\gstapp-1.0.lib" "${GST_LIB_ROOT}\\gstvideo-1.0.lib")
        set(GST_VIDEO_LIBRARIES "${GST_LIB_ROOT}\\gstvideo-1.0.lib")

        set(GLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\glib-2.0" "${GST_LIB_ROOT}\\glib-2.0\\include")
        set(GLIB_LIBRARIES "${GST_LIB_ROOT}\\gobject-2.0.lib" "${GST_LIB_ROOT}\\glib-2.0.lib")

        set(JSONGLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\json-glib-1.0")
        set(JSONGLIB_LIBRARIES "${GST_LIB_ROOT}\\json-glib-1.0.lib")

        if (USE_LIBSOUP2)
            set(LIBSOUP_INCLUDE_DIRS "${GST_ROOT}\\include\\libsoup-2.4")
            set(LIBSOUP_LIBRARIES "${GST_LIB_ROOT}\\soup-2.4.lib")
        else ()
            set(LIBSOUP_INCLUDE_DIRS "${GST_ROOT}\\include\\libsoup-3.0")
            set(LIBSOUP_LIBRARIES "${GST_LIB_ROOT}\\soup-3.0.lib")
        endif ()

        set(GIO_INCLUDE_DIRS "${GST_ROOT}\\include\\gio-win32-2.0")
        set(GIO_LIBRARIES "${GST_LIB_ROOT}\\gio-2.0.lib")
    endif ()
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
        pkg_check_modules(GLIB REQUIRED glib-2.0)
        pkg_check_modules(GST REQUIRED gstreamer-1.0)
        pkg_check_modules(GST REQUIRED gstreamer-plugins-base-1.0)
        pkg_check_modules(GST REQUIRED gstreamer-plugins-bad-1.0)
    endif ()
endif ()

file(COPY assets DESTINATION ${CMAKE_BINARY_DIR}/bin)

add_executable(${PROJECT_NAME}
        src/main.cpp
        src/gui_interface.h
)

if (AVIATEUR_ENABLE_GSTREAMER)
    target_compile_definitions(
            ${PROJECT_NAME} PRIVATE
            AVIATEUR_ENABLE_GSTREAMER
    )
endif ()

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
    target_link_libraries(
            ${PROJECT_NAME} PRIVATE
            ${GST_LIBRARIES}
            ${GST_SDP_LIBRARIES}
            ${GST_WEBRTC_LIBRARIES}
            ${GST_GL_LIBRARIES}
            ${GST_APP_LIBRARIES}
            ${GST_VIDEO_LIBRARIES}
            ${GLIB_LIBRARIES}
            ${GIO_LIBRARIES}
    )

    target_include_directories(
            ${PROJECT_NAME} PRIVATE
            ${GST_INCLUDE_DIRS}
            ${GIO_INCLUDE_DIRS}
            PUBLIC
            ${GLIB_INCLUDE_DIRS}
    )
endif ()

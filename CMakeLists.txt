cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(opendriver VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(OPENDRIVER_BUILD_GUI "Build Qt-based runner/dashboard" ON)
option(OPENDRIVER_ENABLE_LINUX_VIDEO "Build Linux x264/FFmpeg video path" ON)
option(OPENDRIVER_STRICT "Treat warnings as errors for OpenDriver targets" OFF)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Platform detection
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    message(STATUS "Platform: Windows")
    set(OD_PLATFORM_WINDOWS TRUE)
    set(STEAMVR_BIN_DIR "${CMAKE_SOURCE_DIR}/driver/bin/win64")
elseif(UNIX AND NOT APPLE)
    message(STATUS "Platform: Linux")
    set(OD_PLATFORM_LINUX TRUE)
    set(STEAMVR_BIN_DIR "${CMAKE_SOURCE_DIR}/driver/bin/linux64")
elseif(APPLE)
    message(STATUS "Platform: macOS")
    set(OD_PLATFORM_MACOS TRUE)
    set(STEAMVR_BIN_DIR "${CMAKE_SOURCE_DIR}/driver/bin/osx")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Qt6 Configuration (GUI only)
# ─────────────────────────────────────────────────────────────────────────────
if(OPENDRIVER_BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# FetchContent dependencies
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann_json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

# spdlog
FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.11.0.tar.gz
    URL_HASH SHA256=ca5cae8d6cac15dae0ec63b21d6ad3530070650f68076f3a4a862ca293a858bb
)
FetchContent_MakeAvailable(spdlog)

# OpenVR headers
set(OPENVR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openvr/headers)

# ─────────────────────────────────────────────────────────────────────────────
# Video encoding dependencies (Linux only — x264 + FFmpeg)
# ─────────────────────────────────────────────────────────────────────────────
if(OD_PLATFORM_LINUX AND OPENDRIVER_ENABLE_LINUX_VIDEO)
    include(ExternalProject)

    message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
    message(STATUS "📦 Building x264 and FFmpeg (Linux only)...")
    message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

    ExternalProject_Add(x264_static
        GIT_REPOSITORY https://code.videolan.org/videolan/x264.git
        GIT_TAG        master
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --enable-static
            --disable-cli
            --enable-pic
            --extra-cflags=-fPIC
            --disable-asm
            --disable-avs
            --disable-ffms
            --disable-gpac
            --disable-lavf
            --disable-swscale
        BUILD_COMMAND make -j4
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libx264.a
    )

    ExternalProject_Get_Property(x264_static INSTALL_DIR)
    set(X264_INCLUDE_DIRS ${INSTALL_DIR}/include)
    set(X264_LIBRARIES    ${INSTALL_DIR}/lib/libx264.a)

    ExternalProject_Add(ffmpeg_static
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
        GIT_TAG        n6.0
        GIT_SHALLOW    TRUE
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --enable-static
            --disable-shared
            --disable-all
            --disable-autodetect
            --enable-swscale
            --enable-avutil
            --enable-pic
            --extra-cflags="-fPIC"
            --extra-cxxflags="-fPIC"
            --extra-ldflags="-fPIC"
            --disable-asm
            --disable-inline-asm
            --disable-vulkan
        BUILD_COMMAND make -j4
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libswscale.a <INSTALL_DIR>/lib/libavutil.a
    )

    ExternalProject_Get_Property(ffmpeg_static INSTALL_DIR)
    set(SWSCALE_INCLUDE_DIRS ${INSTALL_DIR}/include)
    set(SWSCALE_LIBRARIES    ${INSTALL_DIR}/lib/libswscale.a ${INSTALL_DIR}/lib/libavutil.a)

    include(FindPkgConfig)
    pkg_check_modules(DRM REQUIRED libdrm)

    set(HAVE_VIDEO_ENCODE TRUE)
else()
    message(STATUS "Video encoding (x264/FFmpeg) disabled on this platform")
    set(HAVE_VIDEO_ENCODE FALSE)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# CORE LIBRARY (platform sources conditionally selected)
# ─────────────────────────────────────────────────────────────────────────────

set(CORE_SOURCES
    src/core/event_bus.cpp
    src/core/device_registry.cpp
    src/core/logger.cpp
    src/core/config_manager.cpp
    src/core/plugin_loader.cpp
    src/core/runtime.cpp
    src/core/ipc_platform.cpp   # replaces ipc_linux.cpp
    src/core/process_utils.cpp
)

add_library(opendriver_core STATIC ${CORE_SOURCES})

target_include_directories(opendriver_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENVR_INCLUDE_DIR}
)

target_link_libraries(opendriver_core PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(OD_PLATFORM_LINUX OR OD_PLATFORM_MACOS)
    target_link_libraries(opendriver_core PUBLIC dl pthread)
endif()

if(OD_PLATFORM_WINDOWS)
    # ws2_32 needed for Winsock if ever used; no extra link needed for named pipes
    target_compile_definitions(opendriver_core PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(OPENDRIVER_STRICT)
    if(MSVC)
        target_compile_options(opendriver_core PRIVATE /W4 /WX)
    else()
        target_compile_options(opendriver_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(test_event_bus tests/test_event_bus.cpp)
    target_link_libraries(test_event_bus PRIVATE opendriver_core)
    if(OPENDRIVER_STRICT)
        if(MSVC)
            target_compile_options(test_event_bus PRIVATE /W4 /WX)
        else()
            target_compile_options(test_event_bus PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
    add_test(NAME test_event_bus COMMAND test_event_bus)

    add_executable(test_config_manager tests/test_config_manager.cpp)
    target_link_libraries(test_config_manager PRIVATE opendriver_core)
    if(OPENDRIVER_STRICT)
        if(MSVC)
            target_compile_options(test_config_manager PRIVATE /W4 /WX)
        else()
            target_compile_options(test_config_manager PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
    add_test(NAME test_config_manager COMMAND test_config_manager)

    if(OD_PLATFORM_WINDOWS)
        add_executable(test_mf_color_convert
            tests/test_mf_color_convert.cpp
            src/driver/video/mf_color_convert.cpp
        )
        target_include_directories(test_mf_color_convert PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        if(OPENDRIVER_STRICT)
            if(MSVC)
                target_compile_options(test_mf_color_convert PRIVATE /W4 /WX)
            else()
                target_compile_options(test_mf_color_convert PRIVATE -Wall -Wextra -Wpedantic -Werror)
            endif()
        endif()
        add_test(NAME test_mf_color_convert COMMAND test_mf_color_convert)

        add_library(crash_tick_plugin SHARED tests/plugins/crash_tick_plugin.cpp)
        target_include_directories(crash_tick_plugin PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        set_target_properties(crash_tick_plugin PROPERTIES
            PREFIX ""
            SUFFIX ".dll"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
        )

        add_executable(test_plugin_loader_tick tests/test_plugin_loader_tick.cpp)
        target_link_libraries(test_plugin_loader_tick PRIVATE opendriver_core)
        add_dependencies(test_plugin_loader_tick crash_tick_plugin)
        if(OPENDRIVER_STRICT)
            if(MSVC)
                target_compile_options(test_plugin_loader_tick PRIVATE /W4 /WX)
            else()
                target_compile_options(test_plugin_loader_tick PRIVATE -Wall -Wextra -Wpedantic -Werror)
            endif()
        endif()
        add_test(NAME test_plugin_loader_tick COMMAND test_plugin_loader_tick)
    endif()
endif()

# ----------------------------------------------------------------------------
# BUILT-IN TEST PLUGINS
# ----------------------------------------------------------------------------



# ─────────────────────────────────────────────────────────────────────────────
# RUNNER EXECUTABLE (with embedded GUI)
# ─────────────────────────────────────────────────────────────────────────────

if(OPENDRIVER_BUILD_GUI)
    add_executable(opendriver_runner src/main.cpp)
    target_link_libraries(opendriver_runner PRIVATE opendriver_core)

    target_sources(opendriver_runner PRIVATE
        src/ui/main_window.cpp
        src/ui/device_model.cpp
        src/ui/plugin_model.cpp
    )
    target_link_libraries(opendriver_runner PRIVATE Qt6::Widgets)
    target_compile_definitions(opendriver_runner PRIVATE HAVE_GUI=1)

    if(OD_PLATFORM_WINDOWS)
        set_target_properties(opendriver_runner PROPERTIES WIN32_EXECUTABLE FALSE) # Keep console for debugging even with GUI
        get_filename_component(QT_DEPLOY_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        set(QT_BIN_DIR "${QT_DEPLOY_PREFIX}/bin")
        set(QT_PLUGINS_DIR "${QT_DEPLOY_PREFIX}/plugins")

        add_custom_command(TARGET opendriver_runner POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_BIN_DIR}/Qt6Core.dll"
                "${QT_BIN_DIR}/Qt6Gui.dll"
                "${QT_BIN_DIR}/Qt6Widgets.dll"
                "$<TARGET_FILE_DIR:opendriver_runner>"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:opendriver_runner>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_PLUGINS_DIR}/platforms/qwindows.dll"
                "$<TARGET_FILE_DIR:opendriver_runner>/platforms"
            COMMENT "Deploying Qt runtime dependencies for opendriver_runner"
        )
    endif()

    set_target_properties(opendriver_runner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${STEAMVR_BIN_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STEAMVR_BIN_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STEAMVR_BIN_DIR}"
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# STEAMVR DRIVER (.so on Linux, .dll on Windows)
# ─────────────────────────────────────────────────────────────────────────────

set(DRIVER_SOURCES
    src/driver/driver_opendriver.cpp
)

add_library(driver_opendriver SHARED ${DRIVER_SOURCES})

target_include_directories(driver_opendriver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENVR_INCLUDE_DIR}
)

target_link_libraries(driver_opendriver PRIVATE
    opendriver_core
)

set_target_properties(driver_opendriver PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${STEAMVR_BIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${STEAMVR_BIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${STEAMVR_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${STEAMVR_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STEAMVR_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STEAMVR_BIN_DIR}"
)

# Linux-specific driver settings
if(OD_PLATFORM_LINUX)
    if(HAVE_VIDEO_ENCODE)
        add_dependencies(driver_opendriver x264_static ffmpeg_static)
        target_include_directories(driver_opendriver PRIVATE
            ${DRM_INCLUDE_DIRS}
            ${X264_INCLUDE_DIRS}
            ${SWSCALE_INCLUDE_DIRS}
        )
        target_link_libraries(driver_opendriver PRIVATE
            ${X264_LIBRARIES}
            ${SWSCALE_LIBRARIES}
            ${DRM_LIBRARIES}
        )
        target_compile_definitions(driver_opendriver PRIVATE HAVE_X264=1)
    else()
        target_compile_definitions(driver_opendriver PRIVATE HAVE_X264=0)
    endif()

    target_link_libraries(driver_opendriver PRIVATE pthread)

    # Fix RPATH so driver finds bundled libs without system isolation issues
    set_target_properties(driver_opendriver PROPERTIES
        PREFIX ""
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
        LINK_FLAGS "-Wl,-z,origin -Wl,-rpath,'$ORIGIN'"
    )
endif()

# Windows driver settings
if(OD_PLATFORM_WINDOWS)
    set_target_properties(driver_opendriver PROPERTIES
        PREFIX ""          # No "lib" prefix on Windows
        SUFFIX ".dll"
        WINDOWS_EXPORT_ALL_SYMBOLS FALSE  # Only export explicitly marked symbols
    )
    
    # Add Media Foundation Hardware Encoder sources
    target_sources(driver_opendriver PRIVATE
        src/driver/video/mf_encoder.cpp
        src/driver/video/mf_color_convert.cpp
    )

    # Do not use PIC on Windows
    set_target_properties(driver_opendriver PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    
    # DLL dependencies (OpenVR SDK + standard libs + DXGI/MFT for hardware encoding)
    target_link_libraries(driver_opendriver PRIVATE 
        opendriver_core 
        ${OPENVR_API_LIB}
        ws2_32
        d3d11
        dxgi
        mfplat
        mfuuid
        mfcore
    )
    target_compile_definitions(driver_opendriver PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        HAVE_X264=0
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# DRM SHIM (Linux only — LD_PRELOAD library)
# ─────────────────────────────────────────────────────────────────────────────

if(OD_PLATFORM_LINUX)
    add_library(opendriver_shim SHARED src/driver/drm_lease_shim.cpp)
    target_include_directories(opendriver_shim PRIVATE ${DRM_INCLUDE_DIRS})
    target_link_libraries(opendriver_shim PRIVATE ${DRM_LIBRARIES} dl)
    set_target_properties(opendriver_shim PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${STEAMVR_BIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${STEAMVR_BIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${STEAMVR_BIN_DIR}"
    )
    message(STATUS "DRM lease shim: enabled")
else()
    message(STATUS "DRM lease shim: disabled (Linux only)")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# GUI DASHBOARD (merged into runner, requires Qt6)
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "GUI dashboard: ${OPENDRIVER_BUILD_GUI}")

# ─────────────────────────────────────────────────────────────────────────────
# INSTALL RULES
# ─────────────────────────────────────────────────────────────────────────────

if(OPENDRIVER_BUILD_GUI)
    install(TARGETS opendriver_runner driver_opendriver
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
else()
    install(TARGETS driver_opendriver
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()

if(OD_PLATFORM_LINUX)
    install(TARGETS opendriver_shim LIBRARY DESTINATION lib)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# SUMMARY
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "━━━━━━ OpenDriver ${PROJECT_VERSION} Build Config ━━━━━━")
message(STATUS "  Platform       : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  C++ Standard   : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Video encode   : ${HAVE_VIDEO_ENCODE}")
message(STATUS "  GUI (Qt6)      : ${OPENDRIVER_BUILD_GUI}")
message(STATUS "  DRM shim       : ${OD_PLATFORM_LINUX}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS "")

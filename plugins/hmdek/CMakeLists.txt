cmake_minimum_required(VERSION 3.18)
project(android_hmd_plugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# OPTIONS
# ============================================================================

option(WITH_QT_UI "Build Qt Dashboard UI widget (requires Qt5/Qt6)" OFF)

# ============================================================================
# SOURCES
# ============================================================================

set(PLUGIN_SOURCES
    src/plugin.cpp
    src/udp_receiver.cpp
    src/tcp_sender.cpp
    src/pose_processor.cpp
    src/ui_provider.cpp
)

# ============================================================================
# TARGET
# ============================================================================

add_library(android_hmd_plugin SHARED ${PLUGIN_SOURCES})

target_include_directories(android_hmd_plugin PRIVATE
    .
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../build/_deps/json-src/include
    # Zakładamy że OpenDriver SDK jest w CMAKE_PREFIX_PATH lub ręcznie:
    # ${OPENDRIVER_SDK_DIR}/include
)

# ============================================================================
# DEPENDENCIES
# ============================================================================

# nlohmann/json (header-only, zakładamy że jest dostępny)
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(android_hmd_plugin PRIVATE nlohmann_json::nlohmann_json)
else()
    # Fallback: zakładamy że jest na include path
    message(STATUS "nlohmann_json not found via CMake, assuming it's on include path")
endif()

# Qt (opcjonalnie)
if(WITH_QT_UI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Widgets REQUIRED)
        target_link_libraries(android_hmd_plugin PRIVATE Qt5::Widgets)
    else()
        target_link_libraries(android_hmd_plugin PRIVATE Qt6::Widgets)
    endif()
    target_compile_definitions(android_hmd_plugin PRIVATE WITH_QT_UI)
else()
    # Stub dla IUIProvider bez Qt
    target_compile_definitions(android_hmd_plugin PRIVATE NO_QT_UI)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(android_hmd_plugin PRIVATE ws2_32)
endif()

# Link against OpenDriver Core
# Assume it's in the main build directory
find_library(CORE_LIB opendriver_core 
    HINTS 
    "${CMAKE_CURRENT_SOURCE_DIR}/../../build/Release"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../build/Debug"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../build"
)
if(CORE_LIB)
    target_link_libraries(android_hmd_plugin PRIVATE ${CORE_LIB})
else()
    message(WARNING "opendriver_core library not found! Linking might fail.")
endif()

# ============================================================================
# COMPILER FLAGS
# ============================================================================

if(MSVC)
    target_compile_options(android_hmd_plugin PRIVATE /W4 /O2)
else()
    target_compile_options(android_hmd_plugin PRIVATE -Wall -Wextra -O2)
endif()

# ============================================================================
# INSTALL
# ============================================================================

install(TARGETS android_hmd_plugin
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins  # .dll na Windows
)

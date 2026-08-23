cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_ASIO "Enable ASIO audio capture" ON)

if(NOT ENABLE_ASIO)
    target_disable(win-asio)
    return()
endif()

if(NOT OS_WINDOWS)
    target_disable(win-asio)
    return()
endif()

# Find ASIO SDK
find_path(ASIO_SDK_INCLUDE_DIR asio.h
    PATHS ${CMAKE_SOURCE_DIR}/deps/asiosdk
    NO_DEFAULT_PATH)

if(NOT ASIO_SDK_INCLUDE_DIR)
    message(WARNING "ASIO SDK not found at ${CMAKE_SOURCE_DIR}/deps/asiosdk - ASIO support disabled")
    target_disable(win-asio)
    return()
endif()

message(STATUS "Found ASIO SDK: ${ASIO_SDK_INCLUDE_DIR}")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(win-asio MODULE)
add_library(OBS::asio ALIAS win-asio)

target_sources(win-asio PRIVATE
    plugin-main.cpp
    asio-manager.cpp
    asio-source.cpp
)

target_include_directories(win-asio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASIO_SDK_INCLUDE_DIR}
)

target_link_libraries(win-asio PRIVATE
    OBS::libobs
    ole32
    uuid
    winmm
    avrt
)

# Windows resources
configure_file(cmake/windows/obs-module.rc.in win-asio.rc)
target_sources(win-asio PRIVATE win-asio.rc)

# Set target properties
set_target_properties(win-asio PROPERTIES
    FOLDER "plugins"
    PREFIX ""
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Windows-specific properties
if(MSVC)
    target_compile_options(win-asio PRIVATE
        /MP
        /Zc:__cplusplus
        /Zc:inline
        /Zc:throwingNew
        /permissive-
        /d2FH4-
    )
endif()
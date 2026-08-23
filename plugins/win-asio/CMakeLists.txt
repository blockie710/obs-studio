cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_ASIO "Enable building OBS with native ASIO capture plugin" ON)

if(NOT ENABLE_ASIO)
  target_disable(win-asio)
  return()
endif()

# ASIO SDK is header-only, loaded dynamically at runtime via COM
# No static linking required - we use dynamic discovery via Windows Registry

add_library(win-asio MODULE)
add_library(OBS::win-asio ALIAS win-asio)

find_package(Qt6 REQUIRED Widgets)

target_sources(
  win-asio
  PRIVATE
    asio-driver-manager.cpp
    asio-driver-manager.hpp
    asio-ring-buffer.cpp
    asio-ring-buffer.hpp
    asio-source.cpp
    asio-source.hpp
    win-asio.cpp
    win-asio.hpp
)

target_include_directories(win-asio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  win-asio
  PRIVATE
    OBS::libobs
    Qt::Widgets
    Ole32
    OleAut32
    Uuid
)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in win-asio.rc)
  target_sources(win-asio PRIVATE win-asio.rc)
  set_property(TARGET win-asio APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)
endif()

set_target_properties_obs(
  win-asio
  PROPERTIES FOLDER plugins
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)

# Embed plugin metadata
target_compile_definitions(win-asio PRIVATE
  OBS_PLUGIN_NAME="win-asio"
  OBS_PLUGIN_DESCRIPTION="Native Windows ASIO Audio Capture"
  OBS_PLUGIN_AUTHOR="Community Contributors"
  OBS_PLUGIN_VERSION="${OBS_VERSION_CANONICAL}"
)
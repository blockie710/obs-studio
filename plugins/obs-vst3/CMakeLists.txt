cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_VST3 "Enable building OBS with VST3 plugin host" ON)

if(NOT ENABLE_VST3)
  target_disable(obs-vst3)
  return()
endif()

# VST3 SDK is header-only (or provided via Steinberg VST3 SDK)
# We'll use the VST3 SDK headers - require VST3_SDK_PATH or find_package

find_package(Qt6 REQUIRED Widgets)

# Try to find VST3 SDK
set(VST3_SDK_PATH "" CACHE PATH "Path to Steinberg VST3 SDK")
if(VST3_SDK_PATH AND EXISTS "${VST3_SDK_PATH}/pluginterfaces/vst2.x/aeffect.h")
  message(STATUS "Found VST3 SDK at: ${VST3_SDK_PATH}")
else()
  # Try common locations
  set(VST3_COMMON_PATHS
    "$ENV{VST3_SDK}"
    "C:/Program Files/Steinberg/VST3 SDK"
    "C:/VST3 SDK"
    "/usr/local/include/vst3"
    "/usr/include/vst3"
  )
  foreach(path ${VST3_COMMON_PATHS})
    if(EXISTS "${path}/pluginterfaces/vst2.x/aeffect.h")
      set(VST3_SDK_PATH "${path}" CACHE PATH "Path to Steinberg VST3 SDK" FORCE)
      message(STATUS "Found VST3 SDK at: ${path}")
      break()
    endif()
  endforeach()
endif()

if(NOT VST3_SDK_PATH)
  message(WARNING "VST3 SDK not found. VST3 plugin host will be built with stub implementation.")
endif()

add_library(obs-vst3 MODULE)
add_library(OBS::vst3 ALIAS obs-vst3)

target_sources(
  obs-vst3
  PRIVATE
    vst3-host.cpp
    vst3-host.hpp
    vst3-plugin.cpp
    vst3-plugin.hpp
    vst3-controller.cpp
    vst3-controller.hpp
    vst3-ui.cpp
    vst3-ui.hpp
    obs-vst3.cpp
)

if(VST3_SDK_PATH)
  target_include_directories(obs-vst3 PRIVATE
    "${VST3_SDK_PATH}/pluginterfaces"
    "${VST3_SDK_PATH}/public.sdk/source/vst"
    "${VST3_SDK_PATH}/public.sdk/source/common"
  )
endif()

target_include_directories(obs-vst3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  obs-vst3
  PRIVATE
    OBS::libobs
    Qt::Widgets
    ${CMAKE_DL_LIBS}
)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-vst3.rc)
  target_sources(obs-vst3 PRIVATE obs-vst3.rc)
  set_property(TARGET obs-vst3 APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)
  target_link_libraries(obs-vst3 PRIVATE Ole32 OleAut32 Uuid)
elseif(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  target_link_libraries(obs-vst3 PRIVATE dl pthread)
elseif(OS_MACOS)
  target_link_libraries(obs-vst3 PRIVATE "-framework Cocoa" "-framework Foundation")
endif()

set_target_properties_obs(
  obs-vst3
  PROPERTIES FOLDER plugins
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)

target_compile_definitions(obs-vst3 PRIVATE
  OBS_PLUGIN_NAME="obs-vst3"
  OBS_PLUGIN_DESCRIPTION="Native VST3 Plugin Host Filter"
  OBS_PLUGIN_AUTHOR="Community Contributors"
  OBS_PLUGIN_VERSION="${OBS_VERSION_CANONICAL}"
)
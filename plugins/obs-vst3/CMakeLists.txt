cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_VST3 "Enable building OBS with VST3 plugin host" ON)

if(NOT ENABLE_VST3)
  target_disable(obs-vst3)
  return()
endif()

# VST3 SDK Detection
# Try to find VST3 SDK via:
# 1. VST3_SDK_PATH environment variable / CMake cache
# 2. Steinberg VST3 SDK package (if available via find_package)
# 3. Common install locations

set(VST3_SDK_PATH "" CACHE PATH "Path to Steinberg VST3 SDK (pluginterfaces/)")
set(VST3_SDK_ROOT "" CACHE PATH "Root of VST3 SDK (for public.sdk)")

# Check if path is valid - VST3 SDK uses pluginterfaces/vst/ not vst2.x/
if(VST3_SDK_PATH AND EXISTS "${VST3_SDK_PATH}/pluginterfaces/vst/ivstcomponent.h")
  message(STATUS "VST3 SDK found at: ${VST3_SDK_PATH}")
  set(VST3_SDK_FOUND TRUE)
elseif(VST3_SDK_ROOT AND EXISTS "${VST3_SDK_ROOT}/pluginterfaces/vst/ivstcomponent.h")
  set(VST3_SDK_PATH "${VST3_SDK_ROOT}")
  message(STATUS "VST3 SDK found at: ${VST3_SDK_PATH}")
  set(VST3_SDK_FOUND TRUE)
else()
  # Search common locations
  set(VST3_COMMON_PATHS
    "$ENV{VST3_SDK}"
    "C:/Program Files/Steinberg/VST3 SDK"
    "C:/VST3 SDK"
    "/usr/local/include/vst3"
    "/usr/include/vst3"
    "/opt/vst3sdk"
  )

  foreach(path ${VST3_COMMON_PATHS})
    if(EXISTS "${path}/pluginterfaces/vst/ivstcomponent.h")
      set(VST3_SDK_PATH "${path}" CACHE PATH "Path to Steinberg VST3 SDK" FORCE)
      message(STATUS "Found VST3 SDK at: ${path}")
      set(VST3_SDK_FOUND TRUE)
      break()
    endif()
  endforeach()
endif()

if(NOT VST3_SDK_FOUND)
  message(WARNING "VST3 SDK not found. obs-vst3 will build with stub implementation.")
  message(WARNING "Set VST3_SDK_PATH to enable full VST3 support.")
  add_definitions(-DVST3_STUB_IMPL=1)
else()
  message(STATUS "VST3 SDK: ${VST3_SDK_PATH}")
  add_definitions(-DHAVE_VST3_SDK=1)
endif()

find_package(Qt6 REQUIRED Widgets)

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

target_include_directories(obs-vst3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(VST3_SDK_FOUND)
  target_include_directories(obs-vst3 PRIVATE
    "${VST3_SDK_PATH}/pluginterfaces"
    "${VST3_SDK_PATH}/public.sdk/source/vst"
    "${VST3_SDK_PATH}/public.sdk/source/common"
  )
endif()

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
  target_link_libraries(obs-vst3 PRIVATE "-framework Cocoa" "-framework Foundation" "-framework CoreAudio")
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

# Version resource for Windows
if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-vst3.rc @ONLY)
  target_sources(obs-vst3 PRIVATE obs-vst3.rc)
endif()
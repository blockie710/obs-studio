cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Community-First Independent OBS Studio Hard Fork
# License: GNU GPLv3 (or later)
project(obs-community-studio VERSION ${OBS_VERSION_CANONICAL}
  DESCRIPTION "Community-First Independent OBS Studio Hard Fork"
  HOMEPAGE_URL "https://github.com/obs-community/obs-community-studio"
  LANGUAGES C CXX)

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/architecture.cmake")
  if(NOT OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
    return()
  endif()
endif()

include(compilerconfig)
include(defaults)
include(helpers)

option(ENABLE_FRONTEND "Enable building with UI (requires Qt)" ON)
option(ENABLE_SCRIPTING "Enable scripting support" ON)
option(ENABLE_HEVC "Enable HEVC encoders" ON)

# Native Pro-Audio Options
option(ENABLE_ASIO "Enable native Windows ASIO capture plugin (win-asio)" ON)
option(ENABLE_VST3 "Enable native VST3 plugin host filter (obs-vst3)" ON)

add_subdirectory(libobs)
if(OS_WINDOWS)
  add_subdirectory(libobs-d3d11)
  add_subdirectory(libobs-winrt)
endif()
add_subdirectory(libobs-opengl)
if(OS_MACOS)
  add_subdirectory(libobs-metal)
endif()
add_subdirectory(plugins)

add_subdirectory(test/test-input)

# if(ENABLE_ASIO OR ENABLE_VST3)
#     add_subdirectory(test/audio)
# endif()

add_subdirectory(frontend)

message_configuration()

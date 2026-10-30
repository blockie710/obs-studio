cmake_minimum_required(VERSION 3.28...3.30)

add_library(libobs-d3d12 INTERFACE)
add_library(OBS::libobs-d3d12 ALIAS libobs-d3d12)

target_sources(
  libobs-d3d12
  INTERFACE
    d3d12-fence-awaiter.hpp
    d3d12-subsystem.hpp
)

target_include_directories(libobs-d3d12 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(libobs-d3d12 INTERFACE OBS::libobs)

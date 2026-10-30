cmake_minimum_required(VERSION 3.28...3.30)

add_library(windows-hygiene INTERFACE)
add_library(OBS::windows-hygiene ALIAS windows-hygiene)

target_sources(
  windows-hygiene
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/windows-hygiene.hpp"
)

# Export both shared/ (for <util/windows-hygiene.hpp>) and shared/util/ (for <windows-hygiene.hpp>)
target_include_directories(
  windows-hygiene
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(
  windows-hygiene
  INTERFACE
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

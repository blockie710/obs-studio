# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28...3.30)

# Audio subsystem unit tests
# Minimal test runner (no GTest required)

add_executable(minimal_audio_test
    minimal_test_runner.cpp
)

target_include_directories(minimal_audio_test PRIVATE
    ${CMAKE_SOURCE_DIR}/libobs
    ${CMAKE_SOURCE_DIR}/libobs/media-io
    ${CMAKE_SOURCE_DIR}/plugins/win-asio
    ${CMAKE_SOURCE_DIR}/plugins/obs-vst3
)

target_link_libraries(minimal_audio_test PRIVATE
    OBS::libobs
    ${CMAKE_DL_LIBS}
)

# Add test to CTest
add_test(NAME minimal_audio_test COMMAND minimal_audio_test)
set_tests_properties(minimal_audio_test PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/rundir/RelWithDebInfo/bin/64bit;${CMAKE_BINARY_DIR}/libobs/RelWithDebInfo;$ENV{PATH}"
)

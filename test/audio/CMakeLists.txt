cmake_minimum_required(VERSION 3.28...3.30)

# Audio subsystem unit tests
# These tests require GoogleTest (provided via vcpkg)

if(NOT TARGET GTest::gtest)
    find_package(GTest REQUIRED)
endif()

# ASIO Ring Buffer Tests
add_executable(asio_ring_buffer_test
    asio_ring_buffer_test.cpp
)

target_link_libraries(asio_ring_buffer_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    OBS::libobs
)

# Include path for asio-ring-buffer.hpp
target_include_directories(asio_ring_buffer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/win-asio
)

# Format Converter Tests
add_executable(format_converter_test
    format_converter_test.cpp
)

target_link_libraries(format_converter_test PRIVATE
    GTest::gtest
    GTest::gtest_main
)

# VST3 Automation Tests (requires VST3 SDK for full functionality)
add_executable(vst3_automation_test
    vst3_automation_test.cpp
)

target_link_libraries(vst3_automation_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    obs-vst3
)

target_include_directories(vst3_automation_test PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/obs-vst3
)

# VST3 State Tests (requires VST3 SDK for full functionality)
add_executable(vst3_state_test
    vst3_state_test.cpp
)

target_link_libraries(vst3_state_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    obs-vst3
)

target_include_directories(vst3_state_test PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/obs-vst3
)

# Add tests to CTest
add_test(NAME asio_ring_buffer_test COMMAND asio_ring_buffer_test)
add_test(NAME format_converter_test COMMAND format_converter_test)
add_test(NAME vst3_automation_test COMMAND vst3_automation_test)
add_test(NAME vst3_state_test COMMAND vst3_state_test)

# Enable testing
enable_testing()
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28...3.30)

add_subdirectory(audio)

# Windows Hygiene Stress Test
add_executable(windows_hygiene_test test_windows_hygiene_stress.cpp)
target_include_directories(windows_hygiene_test PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shared)
target_compile_features(windows_hygiene_test PRIVATE cxx_std_23)
add_test(NAME windows_hygiene_test COMMAND windows_hygiene_test)

# Audio Verifier Test
add_executable(audio_verifier_test test_audio_verifier.c ${CMAKE_SOURCE_DIR}/libobs/media-io/audio_verifier.c)
target_include_directories(audio_verifier_test PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libobs)
target_link_libraries(audio_verifier_test PRIVATE OBS::libobs)
add_test(NAME audio_verifier_test COMMAND audio_verifier_test)
set_tests_properties(audio_verifier_test PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/rundir/RelWithDebInfo/bin/64bit;${CMAKE_BINARY_DIR}/libobs/RelWithDebInfo;$ENV{PATH}"
)

if(BUILD_TESTS)
  add_subdirectory(test-input)

  if(OS_WINDOWS)
    add_subdirectory(win)
  endif()

  if(OS_MACOS)
    add_subdirectory(osx)
  endif()
endif()

if(ENABLE_UNIT_TESTS)
  add_subdirectory(cmocka)
endif()

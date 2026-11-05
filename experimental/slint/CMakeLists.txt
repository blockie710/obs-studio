# -----------------------------------------------------------------------------
# LibreCast Studio — Slint pilot (EXPERIMENTAL).
#
# Guarded by the top-level option ENABLE_SLINT_PILOT (default OFF). Building this
# requires network access on first configure (FetchContent pulls Slint) and the
# pilot's only job is to validate toolchain/integration cost — see
# docs/experimental/slint-visual-spec.md §B. It is NOT part of the shipping app.
# -----------------------------------------------------------------------------

# Official Slint CMake integration via FetchContent so a stock configure has no
# system-installed Slint dependency. For an installed binary package, replace these
# three calls with find_package(Slint COMPONENTS Backend) instead.
include(FetchContent)

FetchContent_Declare(
  slint
  GIT_REPOSITORY https://github.com/slint-ui/slint.git
  GIT_TAG        v1.8.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(slint)

add_executable(librecast-slint-pilot
  pilot_main.cpp
)

# Compile BroadcastControlSurface.slint into C++; binds the DSL callbacks/properties
# to pilot_main.cpp (see pilot_main.cpp for the round-trip handlers). Build manually
# with --target librecast-slint-pilot; it is excluded from the default app/install.
slint_target_sources(librecast-slint-pilot
  FILES BroadcastControlSurface.slint
)

target_link_libraries(librecast-slint-pilot
  PRIVATE
    Slint::Slint
)

# Slint minimum is C++11; the repo baseline is C++23 (docs/modernization-implementation-plan.md).
target_compile_features(librecast-slint-pilot PRIVATE cxx_std_23)

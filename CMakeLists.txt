cmake_minimum_required(VERSION 3.20)
project(embedtex LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Host (native) build for StaTeX.
#
# The Teensy image is still built by PlatformIO (platformio.ini); this file
# covers everything that runs on a development host: the StaTeX core, the unit
# suites, the image-similarity engine, and the MicroTeX differential oracle.
#
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# StaTeX core is pure POD C++17 with no heap, exceptions, RTTI or STL, so it
# compiles identically here and on target (docs/StaTeX-dev-process.md §2).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# RelWithDebInfo, not Debug. The suites are dominated by pixel arithmetic --
# the similarity metrics sweep every pixel of both renders several times over
# -- and at -O0 that is an 11x tax on the whole `ctest` run for no diagnostic
# benefit: -g is still on, so a backtrace is still a backtrace. Anyone who
# wants unoptimised frames can ask for -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(STATEX_BUILD_TESTS  "Build the host test suites"                ON)
option(STATEX_BUILD_ORACLE "Build the MicroTeX differential oracle"    ON)
option(STATEX_WERROR       "Treat warnings as errors"                  OFF)

# Warning set applied to first-party code only (never to vendored MicroTeX).
add_library(statex_warnings INTERFACE)
if(MSVC)
  target_compile_options(statex_warnings INTERFACE /W4)
  if(STATEX_WERROR)
    target_compile_options(statex_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(statex_warnings INTERFACE
    -Wall -Wextra -Wshadow -Wfloat-equal -Wsign-conversion)
  if(STATEX_WERROR)
    target_compile_options(statex_warnings INTERFACE -Werror)
  endif()
endif()

# Flags that encode the safety requirements (STX-ERR-01 / STX-TYP-01 /
# STX-BLD-01). Anything linked against `statex` may use exceptions itself; the
# library simply must not need them.
add_library(statex_safety INTERFACE)
if(MSVC)
  target_compile_options(statex_safety INTERFACE /EHs-c- /GR-)
else()
  target_compile_options(statex_safety INTERFACE -fno-exceptions -fno-rtti)
endif()

# ---------------------------------------------------------------------------
# StaTeX core
# ---------------------------------------------------------------------------
file(GLOB STATEX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lib/StaTeX/*.cpp")

add_library(statex STATIC ${STATEX_SOURCES})
target_include_directories(statex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib/StaTeX")
target_link_libraries(statex PRIVATE statex_warnings statex_safety)
# statex_safety is INTERFACE-propagated to consumers of the *core* only where
# they ask for it; the tests deliberately build with exceptions so they can
# host the oracle.

if(STATEX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "StaTeX: tests=${STATEX_BUILD_TESTS} oracle=${STATEX_BUILD_ORACLE} build=${CMAKE_BUILD_TYPE}")

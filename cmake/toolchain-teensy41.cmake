# CMake toolchain for the Teensy 4.1 (i.MX RT1062, Cortex-M7).
#
# Used as:
#   cmake -S targets/teensy41 -B build-teensy41 \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-teensy41.cmake
#
# The cross build cannot be a subdirectory of the host build: one CMake
# configure resolves exactly one toolchain, and the host suites need the host
# compiler. targets/teensy41 is therefore its own project that consumes
# lib/StaTeX as sources.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Where to find arm-none-eabi-*. Override with -DARM_TOOLCHAIN_DIR=... .
# PJRC's toolchain is the default because it is the one that ships
# libarm_cortexM7lfsp_math.a; any arm-none-eabi GCC works if you do not link
# CMSIS DSP (this project does not).
if(NOT ARM_TOOLCHAIN_DIR)
  file(GLOB _candidates
       "$ENV{USERPROFILE}/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin"
       "$ENV{HOME}/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin")
  foreach(_c ${_candidates})
    if(EXISTS "${_c}")
      set(ARM_TOOLCHAIN_DIR "${_c}" CACHE PATH "arm-none-eabi bin directory")
      break()
    endif()
  endforeach()
endif()

if(ARM_TOOLCHAIN_DIR)
  set(_prefix "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
  set(_prefix "arm-none-eabi-")  # fall back to PATH
endif()

# CMake infers the host executable suffix for C/CXX but not for ASM, which
# fails the configure on Windows with an otherwise baffling "not a full path to
# an existing compiler tool".
if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
else()
  set(_exe "")
endif()

set(CMAKE_C_COMPILER   "${_prefix}gcc${_exe}")
set(CMAKE_CXX_COMPILER "${_prefix}g++${_exe}")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_exe}")
set(CMAKE_OBJCOPY      "${_prefix}objcopy${_exe}" CACHE FILEPATH "")
set(CMAKE_SIZE         "${_prefix}size${_exe}"    CACHE FILEPATH "")

# The compiler cannot link a bare executable without the core's startup and
# linker script, so CMake's default compiler probe would fail.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

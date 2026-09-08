cmake_minimum_required(VERSION 3.20)
project(embedtex_teensy41 LANGUAGES C CXX ASM)

# ---------------------------------------------------------------------------
# Teensy 4.1 firmware.
#
#   cmake -S targets/teensy41 -B build-teensy41 \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-teensy41.cmake
#   cmake --build build-teensy41
#   cmake --build build-teensy41 --target flash
#
# Its own project rather than a subdirectory of the root: a cross build needs a
# different toolchain from the host suites, and one CMake configure resolves
# one toolchain. lib/StaTeX is consumed as sources.
#
# Flags below reproduce the PlatformIO build this replaced; they were taken
# from `pio run -v` rather than guessed, so the image is comparable.
# ---------------------------------------------------------------------------

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Vendor SDKs live at the repo root, not under the target: they are shared by
# any board using the same silicon, and keeping them beside the STM32 HAL makes
# "third-party code we never edit" one place rather than one per target.
set(VENDOR "${REPO_ROOT}/vendor/teensy4")

set(TEENSY_CORE "${VENDOR}/core")
set(TEENSY_SPI  "${VENDOR}/SPI")

# --- board / core configuration --------------------------------------------
set(TEENSY_DEFS
    __IMXRT1062__
    ARDUINO_TEENSY41
    ARDUINO=10805
    TEENSYDUINO=159
    CORE_TEENSY
    F_CPU=600000000
    USB_SERIAL
    LAYOUT_US_ENGLISH)

set(TEENSY_ARCH_FLAGS
    -mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16)

add_compile_options(
    ${TEENSY_ARCH_FLAGS}
    -O2 -ffunction-sections -fdata-sections -nostdlib -Wall)
add_compile_definitions(${TEENSY_DEFS})

# The core is third-party and not warning-clean under this project's settings;
# these apply to C++ everywhere, matching what PlatformIO passed.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
                    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
                    $<$<COMPILE_LANGUAGE:CXX>:-felide-constructors>
                    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)

# --- vendored Teensy core ---------------------------------------------------
# Blink.cc is excluded at vendoring time: it defines its own setup()/loop() and
# PJRC documents it as "not actually part of the core library". Globbing *.cc
# here would silently pull it back in and collide with main.cpp.
file(GLOB TEENSY_CORE_SRC CONFIGURE_DEPENDS
     "${TEENSY_CORE}/*.c" "${TEENSY_CORE}/*.cpp" "${TEENSY_CORE}/*.S")

add_library(teensycore STATIC ${TEENSY_CORE_SRC} "${TEENSY_SPI}/SPI.cpp")
target_include_directories(teensycore PUBLIC "${TEENSY_CORE}" "${TEENSY_SPI}")
target_compile_options(teensycore PRIVATE -w)  # vendored: not our warnings

# --- StaTeX core ------------------------------------------------------------
file(GLOB STATEX_SRC CONFIGURE_DEPENDS "${REPO_ROOT}/lib/StaTeX/*.cpp")
add_library(statex STATIC ${STATEX_SRC})
target_include_directories(statex PUBLIC "${REPO_ROOT}/lib/StaTeX")
target_link_libraries(statex PUBLIC teensycore)

# --- display driver ---------------------------------------------------------
# Peripheral-specific, MCU-agnostic: it talks to a drivers::DisplayBus, which
# src/teensy_bus.h implements. Deliberately does NOT link statex -- a driver
# has no business knowing what the application is. The glue lives in backends/.
add_library(ili9488 STATIC "${REPO_ROOT}/drivers/ili9488/ili9488.cpp")
target_include_directories(ili9488 PUBLIC "${REPO_ROOT}/drivers/ili9488")
target_link_libraries(ili9488 PRIVATE teensycore)

# --- backend ----------------------------------------------------------------
# The one component allowed to know both layers. Header-only, so an INTERFACE
# target: it just carries the include path and the two dependencies.
add_library(backend_ili9488 INTERFACE)
target_include_directories(backend_ili9488 INTERFACE
    "${REPO_ROOT}/backends/ili9488")
target_link_libraries(backend_ili9488 INTERFACE statex ili9488)

# --- firmware ---------------------------------------------------------------
add_executable(firmware src/main.cpp)
target_link_libraries(firmware PRIVATE backend_ili9488 teensycore)

set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/imxrt1062_t41_xip.ld")
target_link_options(firmware PRIVATE
    ${TEENSY_ARCH_FLAGS}
    -T "${LINKER_SCRIPT}"
    -Wl,--gc-sections,--relax
    -Wl,-Map,firmware.map
    # The core's RTC starts from this symbol; PlatformIO stamped build time.
    # A fixed value keeps the build reproducible.
    -Wl,--defsym=__rtc_localtime=0)
set_target_properties(firmware PROPERTIES
    LINK_DEPENDS "${LINKER_SCRIPT}"
    OUTPUT_NAME firmware
    SUFFIX ".elf")
target_link_libraries(firmware PRIVATE m stdc++)

# --- hex + size + flash -----------------------------------------------------
add_custom_command(TARGET firmware POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex -R .eeprom
            "$<TARGET_FILE:firmware>" "${CMAKE_BINARY_DIR}/firmware.hex"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:firmware>"
    COMMENT "Generating firmware.hex")

# Upload uses teensy_post_compile, which ships with Teensyduino and inside
# PlatformIO's tool-teensy. It is the tool that actually reboots a *running*
# sketch into the bootloader and hands the hex to the loader.
#
# teensy_loader_cli is the obvious alternative and is a trap here: `-w` blocks
# forever waiting for a physical button press, and `-s` (soft reboot) did not
# take on this board. teensy_post_compile -reboot is the path PlatformIO used
# and the one that works unattended.
find_path(TEENSY_TOOLS
    NAMES teensy_post_compile teensy_post_compile.exe
    HINTS "$ENV{USERPROFILE}/.platformio/packages/tool-teensy"
          "$ENV{HOME}/.platformio/packages/tool-teensy")

if(TEENSY_TOOLS)
  add_custom_target(flash
      COMMAND "${TEENSY_TOOLS}/teensy_post_compile"
              "-file=firmware" "-path=${CMAKE_BINARY_DIR}"
              "-tools=${TEENSY_TOOLS}" "-board=TEENSY41" "-reboot"
      DEPENDS firmware
      USES_TERMINAL
      COMMENT "Flashing firmware.hex to Teensy 4.1")
else()
  add_custom_target(flash
      COMMAND "${CMAKE_COMMAND}" -E echo
              "teensy tools not found; configure with -DTEENSY_TOOLS=/path/to/tool-teensy"
      COMMAND "${CMAKE_COMMAND}" -E false)
endif()

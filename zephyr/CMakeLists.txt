if(CONFIG_STATEX)

zephyr_library_named(statex)

set(STATEX_ROOT ${ZEPHYR_CURRENT_MODULE_DIR})

file(GLOB STATEX_GLYPHSTORE_SOURCES CONFIGURE_DEPENDS
  ${STATEX_ROOT}/lib/glyphstore/*.cpp)
file(GLOB STATEX_CORE_SOURCES CONFIGURE_DEPENDS
  ${STATEX_ROOT}/lib/StaTeX/*.cpp)

zephyr_library_sources(${STATEX_GLYPHSTORE_SOURCES} ${STATEX_CORE_SOURCES})
zephyr_library_sources(${STATEX_ROOT}/backends/statex_rgb565/statex_rgb565.cpp)

# Consumers include <statex_render.h> etc. directly, as in the host build.
zephyr_include_directories(
  ${STATEX_ROOT}/lib/glyphstore
  ${STATEX_ROOT}/lib/StaTeX
  ${STATEX_ROOT}/backends/statex_rgb565)

# The safety requirements (STX-ERR-01/STX-TYP-01) — Zephyr already builds
# C++ without exceptions/RTTI unless the application opts in; restate them
# here so this library stays safe even in such an image.
zephyr_library_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

endif()

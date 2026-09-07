# ---------------------------------------------------------------------------
# The MicroTeX differential oracle (docs/StaTeX-dev-process.md §4).
#
# lib/MicroTeX is the dynamic implementation StaTeX was rewritten from. It is
# built HERE ONLY, for tests, and never linked into the device image
# (STX-BLD-02) -- platformio.ini's build_src_filter and lib_deps do not
# reference it.
#
# It is built with exceptions and RTTI on, because it needs them; StaTeX
# itself stays -fno-exceptions -fno-rtti. Mixing is fine at link time since
# StaTeX contains nothing that throws.
#
# Its own warnings are suppressed: it is vendored third-party code and we are
# not going to clean it up.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE MICROTEX_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/lib/MicroTeX/*.cpp")
# graphic_tft.cpp is the Teensy/TFT port: it needs TFT_eSPI and
# OpenFontRender. tests/oracle/microtex_host.cpp replaces it.
list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "graphic/graphic_tft\\.cpp$")

add_library(microtex STATIC
  ${MICROTEX_SOURCES}
  "${CMAKE_SOURCE_DIR}/lib/tinyxml/tinyxml2.cpp"
)
target_include_directories(microtex PUBLIC
  "${CMAKE_SOURCE_DIR}/lib/MicroTeX"
  "${CMAKE_SOURCE_DIR}/lib/tinyxml"
)
# MICROTEX_EMBEDDED makes LaTeX::init treat the resource path as authoritative
# instead of probing the filesystem; all the tables we need are the compiled-in
# res/builtin/*.res.cpp ones, so nothing is ever read from disk.
# NDEBUG turns off CLATEX_DEBUG (config.h), and
# the two TRACE switches silence the parser's printf tracing -- the learning
# pass alone runs ~150 parses and the trace buries the test output.
target_compile_definitions(microtex PUBLIC
  MICROTEX_EMBEDDED
  NDEBUG
  MICROTEX_TRACE_PARSE=0
  MICROTEX_TRACE_PARSER=0)
if(MSVC)
  target_compile_options(microtex PRIVATE /w)
else()
  target_compile_options(microtex PRIVATE -w)
endif()
set_target_properties(microtex PROPERTIES CXX_STANDARD 17)

# --- the oracle adapter -----------------------------------------------------
# The reference is rasterised with the real Computer Modern faces in fonts/,
# so the oracle side is genuine TeX output rather than StaTeX's atlas
# pretending to be one. That needs FreeType.
add_library(stx_oracle STATIC
  microtex_host.cpp
  microtex_oracle.cpp
  ft_raster.cpp
)
target_include_directories(stx_oracle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(stx_oracle PUBLIC microtex stx_testsupport Freetype::Freetype)
target_compile_definitions(stx_oracle PRIVATE
  STATEX_TEX_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")
# The adapter touches MicroTeX types, so it needs RTTI and exceptions too.

# --- the differential suite -------------------------------------------------
# RED: StaTeX's layout constants were hand-picked (statex_fontparams.h) rather
# than derived from MicroTeX's tables, and its glyph metrics come from Latin
# Modern via tools/genfont rather than from res/font/*.def.cpp. Dev-process §4
# calls that out as the precondition for the comparison to mean anything.
stx_add_suite(test_oracle_diff test_oracle_diff.cpp
              LABELS "red;oracle" LIBS stx_oracle)
# Green: the oracle adapter itself must be sane before its verdicts count.
stx_add_suite(test_oracle_selftest test_oracle_selftest.cpp
              LABELS "green;oracle" LIBS stx_oracle)

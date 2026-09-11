# ---------------------------------------------------------------------------
# Host test suites.
#
# Three groups, distinguished by ctest label:
#
#   green  - must pass. The gate.        ctest -L green
#   red    - known-open defects. These FAIL on purpose and stay failing until
#            the defect is fixed (docs/StaTeX-dev-process.md §1: write the
#            failing test first).                       ctest -L red
#   oracle - differential comparison against vendored MicroTeX. Red today,
#            because StaTeX's layout constants were hand-picked rather than
#            taken from MicroTeX's tables.              ctest -L oracle
#
# `ctest` with no selector runs everything and will therefore report failures;
# that is intended. Use `ctest -L green` for a clean/dirty signal.
# ---------------------------------------------------------------------------

set(STATEX_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
file(MAKE_DIRECTORY "${STATEX_ARTIFACT_DIR}")

# --- shared test support ---------------------------------------------------
add_library(stx_testsupport STATIC
  support/stx_image.cpp
  support/stx_compare.cpp
  support/stx_render.cpp
  support/stx_corpus.cpp
  support/stx_png.cpp
  support/stx_report.cpp
)
target_include_directories(stx_testsupport PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/framework"
  "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(stx_testsupport PUBLIC statex PRIVATE statex_warnings)
target_compile_definitions(stx_testsupport PUBLIC
  STATEX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens")

# Helper: declare one test executable from one source file.
#
# SAFETY  builds the suite itself with -fno-exceptions -fno-rtti and links only
#         the StaTeX core. The unit suites need this: test_buildflags asserts
#         the flags with #error, and the point of those suites is to exercise
#         StaTeX under the same constraints the target imposes (STX-BLD-01).
#         Everything else links stx_testsupport, which is STL-heavy and (for
#         the oracle) needs exceptions.
function(stx_add_suite name source)
  cmake_parse_arguments(ARG "SAFETY" "" "LABELS;LIBS" ${ARGN})
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/support")
  if(ARG_SAFETY)
    target_link_libraries(${name} PRIVATE statex statex_warnings statex_safety ${ARG_LIBS})
  else()
    target_link_libraries(${name} PRIVATE stx_testsupport statex_warnings ${ARG_LIBS})
  endif()
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    LABELS "${ARG_LABELS}"
    ENVIRONMENT "STATEX_TEST_ARTIFACTS=${STATEX_ARTIFACT_DIR}")
endfunction()

# --- tests/unit: the per-module suites -------------------------------------
# Moved here from the old top-level test/ directory so there is one test tree,
# not two. They were written against PlatformIO's Unity and are unchanged:
# tests/framework/unity.h is a drop-in replacement. PlatformIO still runs them
# via `test_dir = tests/unit` in platformio.ini.
file(GLOB UNIT_SUITE_DIRS
     RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/unit"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*")
foreach(dir ${UNIT_SUITE_DIRS})
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/unit/${dir}/${dir}.cpp")
  if(EXISTS "${src}")
    stx_add_suite(${dir} "${src}" SAFETY LABELS "green;unit")
  endif()
endforeach()

# --- tests/behaviour: whole-pipeline properties ----------------------------
# Green. The similarity engine has to be trustworthy before anything can lean
# on it, so it gets its own self-tests first.
stx_add_suite(test_imgcompare      behaviour/test_imgcompare.cpp      LABELS "green;behaviour")
stx_add_suite(test_corpus_refusals behaviour/test_corpus_refusals.cpp LABELS "green;behaviour")

# Formerly red. Each of these pinned an open defect; the defect was fixed and
# the suite now guards the behaviour it specified. Kept verbatim -- they went
# green by the code changing, not by an assertion being relaxed.
stx_add_suite(test_glyph_coverage  behaviour/test_glyph_coverage.cpp  LABELS "green;behaviour")
stx_add_suite(test_sdf_rounding    behaviour/test_sdf_rounding.cpp    LABELS "green;behaviour")
stx_add_suite(test_matrix_metrics  behaviour/test_matrix_metrics.cpp  LABELS "green;behaviour")
stx_add_suite(test_draw_atomicity  behaviour/test_draw_atomicity.cpp  LABELS "green;behaviour")
stx_add_suite(test_node_bounds     behaviour/test_node_bounds.cpp     LABELS "green;behaviour")
stx_add_suite(test_atom_spacing    behaviour/test_atom_spacing.cpp    LABELS "green;behaviour")
stx_add_suite(test_radical         behaviour/test_radical.cpp         LABELS "green;behaviour")
stx_add_suite(test_a8_backend      behaviour/test_a8_backend.cpp      LABELS "green;behaviour" LIBS statex_a8)

# Its own entry on purpose. The corpus covers the fourteen symbol names that
# were the whole command set when it was written; this sweeps all six hundred,
# and folding it into the oracle suite would bury it under a red result that
# is red for unrelated layout reasons.
stx_add_suite(test_symbol_sweep    behaviour/test_symbol_sweep.cpp    LABELS "green;behaviour")

# --- tests/defects: one suite per open defect ------------------------------
# EMPTY. Every defect suite written for this effort has been fixed and moved to
# tests/behaviour/ with its assertions unchanged. The directory stays because
# the process (dev-process.md §1: write the failing test first) puts the next
# one here.

# --- MicroTeX differential oracle ------------------------------------------
# Needs FreeType, which rasterises the reference with the real TeX faces. It is
# a host dev dependency only -- nothing on the device path uses it -- so a
# machine without it still gets the green gate and the defect suites.
if(STATEX_BUILD_ORACLE)
  find_package(Freetype QUIET)
  if(Freetype_FOUND)
    add_subdirectory(oracle)
  else()
    message(WARNING
      "FreeType not found -- the MicroTeX differential oracle is disabled. "
      "Install it (MSYS2: pacman -S mingw-w64-x86_64-freetype; "
      "Debian: libfreetype-dev) or configure with -DSTATEX_BUILD_ORACLE=OFF "
      "to silence this.")
  endif()
endif()

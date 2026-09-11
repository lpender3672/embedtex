#!/usr/bin/env python3
"""Generate a target's lv_conf.h from LVGL's template.

Editing 1300 lines by hand and re-doing it on every LVGL bump is how a config
drifts. This applies a named set of overrides to vendor/lvgl/lv_conf_template.h
and writes the result, so the diff from upstream is the list below rather than
a whole file nobody re-reads.

    python tools/make_lv_conf.py targets/teensy41/lv_conf.h
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEMPLATE = REPO / "vendor" / "lvgl" / "lv_conf_template.h"

# (name, value, why) -- the "why" is only for the ones that are not obvious.
SETTINGS: list[tuple[str, str, str]] = [
    # --- colour -----------------------------------------------------------
    # The ILI9488 is 18-bit over SPI and takes three bytes per pixel whatever
    # LVGL renders. lv_color_t is {blue, green, red} in v9, so RGB888 -> the
    # panel is an in-place three-byte reversal; RGB565 would need a second
    # staging buffer, because 2->3 bytes cannot expand in place.
    ("LV_COLOR_DEPTH", "24", "18-bit panel; see targets/teensy41/README.md"),

    # --- memory -----------------------------------------------------------
    # A fixed TLSF pool in a static array, not newlib's malloc growing through
    # _sbrk. Same discipline as StaTeX's g_scratch (STX-MEM-02), and it gives
    # lv_mem_monitor() for a measured high-water mark.
    ("LV_USE_STDLIB_MALLOC", "LV_STDLIB_BUILTIN", ""),
    ("LV_USE_STDLIB_STRING", "LV_STDLIB_BUILTIN", ""),
    ("LV_USE_STDLIB_SPRINTF", "LV_STDLIB_BUILTIN", ""),
    ("LV_MEM_SIZE", "(24 * 1024U)", "measured max_used 4.2 KB + an 8 KB simple layer"),
    ("LV_MEM_POOL_EXPAND_SIZE", "0", ""),
    ("LV_MEM_ADR", "0", ""),
    # Defaults to 24 KB and is allocated *out of* LV_MEM_SIZE the first time any
    # widget needs a simple layer (style_opa < 255, any transform). At a 48 KB
    # pool that is most of it.
    ("LV_DRAW_LAYER_SIMPLE_BUF_SIZE", "(8 * 1024)", ""),

    # --- draw -------------------------------------------------------------
    ("LV_USE_DRAW_SW", "1", ""),
    ("LV_DRAW_SW_DRAW_UNIT_CNT", "1", ">1 needs LV_USE_OS"),
    ("LV_USE_DRAW_ARM2D_SYNC", "0", ""),
    ("LV_USE_NATIVE_HELIUM_ASM", "0", "Cortex-M7 has no MVE"),
    ("LV_DRAW_SW_COMPLEX", "1", ""),
    ("LV_DRAW_SW_SHADOW_CACHE_SIZE", "0", ""),
    ("LV_DRAW_SW_CIRCLE_CACHE_SIZE", "0", ""),
    ("LV_DRAW_BUF_STRIDE_ALIGN", "1", "so an A8 buffer's stride == its width"),
    ("LV_DRAW_BUF_ALIGN", "4", ""),

    # --- core -------------------------------------------------------------
    ("LV_USE_OS", "LV_OS_NONE", ""),
    ("LV_DEF_REFR_PERIOD", "33", ""),
    ("LV_DPI_DEF", "165", "sqrt(480^2+320^2)/3.5in"),
    ("LV_USE_LOG", "1", "bring-up; costs .rodata, so turn off when shipping"),
    ("LV_LOG_LEVEL", "LV_LOG_LEVEL_WARN", ""),
    ("LV_USE_ASSERT_NULL", "1", ""),
    ("LV_USE_ASSERT_MALLOC", "1", ""),

    # --- fonts ------------------------------------------------------------
    ("LV_FONT_MONTSERRAT_14", "1", "LV_FONT_DEFAULT must point at an enabled font"),
    ("LV_FONT_DEFAULT", "&lv_font_montserrat_14", ""),

    # --- widgets we do not use -------------------------------------------
    # Disabled features compile to empty objects, so this is about flash and
    # (on this target especially) DTCM, not about correctness.
    ("LV_USE_CANVAS", "0", "lv_image is enough for an A8 coverage buffer"),
    ("LV_USE_ANIMIMG", "0", ""),
    ("LV_USE_CALENDAR", "0", ""),
    ("LV_USE_CHART", "0", ""),
    ("LV_USE_IMAGEBUTTON", "0", ""),
    ("LV_USE_KEYBOARD", "0", ""),
    ("LV_USE_LED", "0", ""),
    ("LV_USE_LOTTIE", "0", ""),
    ("LV_USE_MENU", "0", ""),
    ("LV_USE_MSGBOX", "0", ""),
    ("LV_USE_SPAN", "0", ""),
    ("LV_USE_SPINBOX", "0", ""),
    ("LV_USE_SPINNER", "0", ""),
    ("LV_USE_TABVIEW", "0", ""),
    ("LV_USE_TILEVIEW", "0", ""),
    ("LV_USE_WIN", "0", ""),
    ("LV_USE_3DTEXTURE", "0", ""),

    # --- heavyweight libs, decoders, filesystems --------------------------
    ("LV_USE_THORVG_INTERNAL", "0", "its CMake glob is empty -> add_library() error"),
    ("LV_USE_THORVG_EXTERNAL", "0", ""),
    ("LV_USE_VECTOR_GRAPHIC", "0", ""),
    ("LV_USE_FREETYPE", "0", ""),
    ("LV_USE_TINY_TTF", "0", ""),
    ("LV_USE_LODEPNG", "0", ""),
    ("LV_USE_LIBPNG", "0", ""),
    ("LV_USE_BMP", "0", ""),
    ("LV_USE_TJPGD", "0", ""),
    ("LV_USE_LIBJPEG_TURBO", "0", ""),
    ("LV_USE_GIF", "0", ""),
    ("LV_USE_QRCODE", "0", ""),
    ("LV_USE_BARCODE", "0", ""),
    ("LV_USE_RLE", "0", ""),
    ("LV_USE_FFMPEG", "0", ""),
    ("LV_USE_RLOTTIE", "0", ""),
    ("LV_USE_SNAPSHOT", "0", ""),
    ("LV_USE_SYSMON", "0", ""),
    ("LV_USE_PROFILER", "0", ""),
    ("LV_USE_MONKEY", "0", ""),
    ("LV_USE_IME_PINYIN", "0", ""),
    ("LV_USE_FILE_EXPLORER", "0", ""),
    ("LV_USE_FONT_MANAGER", "0", ""),
    ("LV_USE_TEST", "0", ""),
    ("LV_BUILD_EXAMPLES", "0", ""),
    ("LV_USE_DEMO_WIDGETS", "0", ""),
    ("LV_USE_DEMO_BENCHMARK", "0", ""),
    ("LV_USE_DEMO_STRESS", "0", ""),
    ("LV_USE_DEMO_MUSIC", "0", ""),

    # --- caches -----------------------------------------------------------
    # Load-bearing for the A8 path: with the cache off, lv_image_decoder_open
    # re-reads our bytes on every draw, which is what makes "mutate the buffer
    # in place, then lv_obj_invalidate()" correct. Turn it on and you must call
    # lv_image_cache_drop() after every content change or you render stale ink.
    ("LV_CACHE_DEF_SIZE", "0", ""),
    ("LV_IMAGE_HEADER_CACHE_DEF_CNT", "0", ""),
]

# Appended verbatim after the overrides. LV_ATTRIBUTE_LARGE_CONST is the one
# that matters on Teensy: this linker script routes *(.rodata*) into DTCM, so
# font bitmaps would otherwise eat RAM1. `.progmem` stays in flash -- the same
# trick STATEX_FLASH already uses for the SDF atlas.
EPILOGUE = """
/* ---------------------------------------------------------------------------
 * embedtex additions (tools/make_lv_conf.py)
 * ------------------------------------------------------------------------ */

/* This target's linker script routes *(.rodata*) into .data > DTCM, so LVGL's
 * const font bitmaps would be copied into RAM1 at boot. `.progmem` is kept in
 * flash, exactly as STATEX_FLASH does for the glyph atlas. Note this attribute
 * covers glyph_bitmap only; glyph_dsc, cmaps and the kern tables are plain
 * `static const` and still land in DTCM. */
#define LV_ATTRIBUTE_LARGE_CONST __attribute__((section(".progmem")))
"""


def set_define(text: str, name: str, value: str) -> tuple[str, bool]:
    """Replace the value of `#define name ...`, preserving any trailing comment."""
    pattern = re.compile(
        r"^(\s*#define\s+" + re.escape(name) + r")([ \t]+)([^\r\n]*?)(\s*/\*.*)?$",
        re.MULTILINE,
    )

    def repl(m: re.Match) -> str:
        return f"{m.group(1)}{m.group(2)}{value}{m.group(4) or ''}"

    new, n = pattern.subn(repl, text, count=1)
    return new, n > 0


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    if not TEMPLATE.is_file():
        print(f"error: {TEMPLATE} not found -- is vendor/lvgl populated?",
              file=sys.stderr)
        return 2

    text = TEMPLATE.read_text(encoding="utf-8")

    # The template ships disabled. Forgetting this yields a working build
    # against every LVGL default, announced only by a #pragma message.
    if "#if 0 /* Set this to \"1\" to enable content */" not in text:
        print("warning: template's `#if 0` guard not found; check LVGL version",
              file=sys.stderr)
    text = text.replace('#if 0 /* Set this to "1" to enable content */',
                        '#if 1 /* enabled by tools/make_lv_conf.py */', 1)

    missing = []
    for name, value, _why in SETTINGS:
        text, ok = set_define(text, name, value)
        if not ok:
            missing.append(name)

    # Insert the epilogue before the closing guard rather than after it.
    marker = "#endif /*LV_CONF_H*/"
    if marker in text:
        text = text.replace(marker, EPILOGUE + "\n" + marker, 1)
    else:
        text += EPILOGUE

    header = (
        "/* GENERATED by tools/make_lv_conf.py from vendor/lvgl/"
        "lv_conf_template.h -- do not edit.\n"
        " * Change the SETTINGS list in that script and regenerate. */\n"
    )
    out.write_text(header + text, encoding="utf-8", newline="\n")

    print(f"wrote {out} ({len(text.splitlines())} lines, "
          f"{len(SETTINGS)} overrides)")
    if missing:
        print(f"WARNING: {len(missing)} setting(s) not found in the template "
              f"-- LVGL may have renamed them:", file=sys.stderr)
        for m in missing:
            print(f"  {m}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

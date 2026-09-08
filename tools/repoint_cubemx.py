#!/usr/bin/env python3
"""Re-apply the vendor/ repoint after STM32CubeMX regenerates a target.

CubeMX writes ST-authored code (Drivers/, startup_*.s, system_*.c) next to the
.ioc, and regenerates cmake/stm32cubemx/CMakeLists.txt pointing at it. This
repo keeps that code in vendor/stm32u5xx/ instead, shared across every board on
the same silicon.

Run this after any regeneration:

    python tools/repoint_cubemx.py targets/stm32u5g9j-dk

It rewrites the generated CMake to reference vendor/, and deletes the copies
CubeMX recreated. Idempotent: running it on an already-repointed tree is a
no-op.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

BANNER = "# REPOINTED BY tools/repoint_cubemx.py"

# (what CubeMX emits, where we keep it) -- relative to the generated CMake file.
REWRITES = [
    ("${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/",
     "${CMAKE_CURRENT_SOURCE_DIR}/../../../../vendor/@VENDOR@/"),
    ("${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_",
     "${CMAKE_CURRENT_SOURCE_DIR}/../../../../vendor/@VENDOR@/system_"),
    ("${CMAKE_CURRENT_SOURCE_DIR}/../../startup_",
     "${CMAKE_CURRENT_SOURCE_DIR}/../../../../vendor/@VENDOR@/startup_"),
]

# Copies CubeMX recreates that are now owned by vendor/.
REDUNDANT_DIRS = ["Drivers"]
REDUNDANT_GLOBS = ["startup_*.s", "Core/Src/system_*.c"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", type=Path,
                    help="target directory holding the .ioc, e.g. targets/stm32u5g9j-dk")
    ap.add_argument("--vendor", default="stm32u5xx",
                    help="subdirectory of vendor/ holding the SDK (default: stm32u5xx)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    target: Path = args.target
    gen = target / "cmake" / "stm32cubemx" / "CMakeLists.txt"
    if not gen.is_file():
        print(f"error: {gen} not found -- is that a CubeMX target?", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parent.parent
    vendor_dir = repo_root / "vendor" / args.vendor
    if not vendor_dir.is_dir():
        print(f"error: {vendor_dir} does not exist", file=sys.stderr)
        return 2

    text = gen.read_text(encoding="utf-8")
    before = text
    for src, dst in REWRITES:
        text = text.replace(src, dst.replace("@VENDOR@", args.vendor))

    changed = text != before
    if changed and not text.startswith(BANNER):
        text = (f"{BANNER}\n"
                f"# ST-authored sources live in vendor/{args.vendor}/; CubeMX emits them\n"
                f"# beside the .ioc. Re-run that script after every regeneration.\n") + text

    if args.dry_run:
        print(f"{'would rewrite' if changed else 'already repointed'}: {gen}")
    elif changed:
        gen.write_text(text, encoding="utf-8", newline="\n")
        print(f"repointed: {gen}")
    else:
        print(f"already repointed: {gen}")

    # Remove what CubeMX recreated, but only once the vendor copy really exists.
    removed = []
    for name in REDUNDANT_DIRS:
        p = target / name
        if p.is_dir():
            removed.append(p)
            if not args.dry_run:
                shutil.rmtree(p)
    for pattern in REDUNDANT_GLOBS:
        for p in target.glob(pattern):
            if p.is_file():
                removed.append(p)
                if not args.dry_run:
                    p.unlink()

    for p in removed:
        print(f"{'would remove' if args.dry_run else 'removed'}: "
              f"{p.relative_to(target.parent.parent) if repo_root in p.resolve().parents else p}")
    if not removed:
        print("no regenerated vendor copies to remove")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

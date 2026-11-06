#!/usr/bin/env python3
"""Compile and run a synthetic PS2Vfs-to-cdRoot adapter smoke test.

The pinned Waffle source tree must already have been prepared by
build_recompiler_host.py. No game files are read or generated.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--work-dir",
        default=os.environ.get("GOW_PRIVATE_WORKDIR", "/tmp/gowport"),
        help="private workspace previously prepared by build_recompiler_host.py",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    work = Path(args.work_dir).expanduser().resolve()
    source = work / "sources" / "PS2Recomp"
    runtime = source / "ps2xRuntime"
    iop = source / "ps2xIOP"

    sources = [
        repo_root / "tools" / "ps2_vfs_cdroot_smoke.cpp",
        repo_root / "android" / "app" / "src" / "main" / "cpp" / "cdroot_vfs.cpp",
        runtime / "src" / "lib" / "ps2_vfs.cpp",
        runtime / "src" / "lib" / "ps2_rom_device.cpp",
        iop / "src" / "ps2_path.cpp",
    ]
    missing = [str(path) for path in sources if not path.is_file()]
    if missing:
        parser.error(
            "Required source files are missing. Run build_recompiler_host.py first: "
            + ", ".join(missing)
        )

    compiler = os.environ.get("CXX") or shutil.which("c++")
    if not compiler:
        parser.error("A host C++ compiler was not found (set CXX or install c++).")

    with tempfile.TemporaryDirectory(prefix="gow-ps2-vfs-smoke-") as temporary:
        executable = Path(temporary) / "ps2-vfs-cdroot-smoke"
        command = [
            compiler,
            "-std=c++20",
            "-D__ANDROID__",
            "-Wall",
            "-Wextra",
            "-I",
            str(repo_root / "android" / "app" / "src" / "main" / "cpp"),
            "-I",
            str(runtime / "include"),
            "-I",
            str(iop / "include"),
            *(str(path) for path in sources),
            "-o",
            str(executable),
        ]
        print("+ " + " ".join(command), flush=True)
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

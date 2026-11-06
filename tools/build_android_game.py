#!/usr/bin/env python3
"""Build a local Android APK containing AOT generated from the user's own PS2 ELF.

The ELF, generated C++ and host recompiler workspace stay outside tracked source:
- source ELF: supplied by the user, outside this repository;
- host work tree: outside this repository;
- generated C++: android/app/build/gow-generated-game (Git-ignored);
- resulting APK: android/app/build/outputs/apk/debug/app-debug.apk.

This command never uploads or commits game-derived data.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class BuildPlan:
    elf: Path
    work_dir: Path
    generated_dir: Path
    recompiler_command: tuple[str, ...]
    gradle_command: tuple[str, ...]
    apk_path: Path


def _is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def create_build_plan(
    repo_root: Path,
    elf_path: Path,
    work_dir: Path,
    *,
    python_executable: str | None = None,
    windows: bool | None = None,
) -> BuildPlan:
    """Validate private paths and create commands without executing them."""
    root = repo_root.expanduser().resolve()
    elf = elf_path.expanduser().resolve()
    work = work_dir.expanduser().resolve()
    use_windows = os.name == "nt" if windows is None else windows
    python = python_executable or sys.executable

    if not elf.is_file():
        raise ValueError(f"PS2 ELF file was not found: {elf}")
    if _is_within(elf, root):
        raise ValueError(
            "Keep the user's ELF outside the public repository. "
            "Choose a private local path such as C:/gowport/input/SCUS_973.99."
        )
    if _is_within(work, root):
        raise ValueError(
            "The host recompiler workspace must be outside the Git repository."
        )
    if not str(work).isascii():
        raise ValueError(
            "The private workspace path must use ASCII characters, "
            "for example C:/gowport or /tmp/gowport."
        )

    generated = (root / "android/app/build/gow-generated-game").resolve()
    build_root = (root / "android/app/build").resolve()
    if not _is_within(generated, build_root) or generated == build_root:
        raise ValueError("Generated C++ staging must remain inside android/app/build.")

    wrapper = root / ("gradlew.bat" if use_windows else "gradlew")
    if not wrapper.is_file():
        raise ValueError(f"Gradle wrapper is missing: {wrapper}")

    generator = root / "tools/build_recompiler_host.py"
    if not generator.is_file():
        raise ValueError(f"Host recompiler entry point is missing: {generator}")

    recompiler_command = (
        python,
        str(generator),
        "--elf",
        str(elf),
        "--work-dir",
        str(work),
        "--export-game-cpp-dir",
        str(generated),
    )
    gradle_command = (
        str(wrapper),
        "--no-daemon",
        "assembleDebug",
    )
    return BuildPlan(
        elf=elf,
        work_dir=work,
        generated_dir=generated,
        recompiler_command=recompiler_command,
        gradle_command=gradle_command,
        apk_path=root / "android/app/build/outputs/apk/debug/app-debug.apk",
    )


def _run(command: tuple[str, ...], cwd: Path) -> None:
    print("$ " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=str(cwd), check=True)


def _require_ignored_generated_dir(root: Path, generated_dir: Path) -> None:
    relative = generated_dir.relative_to(root).as_posix()
    result = subprocess.run(
        ["git", "-C", str(root), "check-ignore", "-q", "--", relative],
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Generated staging is not Git-ignored: {relative}. "
            "Do not generate or build game code until .gitignore protects it."
        )


def _verify_generated_outputs(generated_dir: Path) -> int:
    required = (
        "register_functions.cpp",
        "ps2_recompiled_functions.h",
        "ps2_recompiled_stubs.h",
    )
    missing = [name for name in required if not (generated_dir / name).is_file()]
    function_cpp = [
        path for path in generated_dir.rglob("*.cpp")
        if path.name != "register_functions.cpp"
    ]
    if missing or not function_cpp:
        raise RuntimeError(
            "The private recompiler output is incomplete. "
            f"Missing: {', '.join(missing) or 'none'}; "
            f"function translation units: {len(function_cpp)}."
        )
    return len(function_cpp) + 1


def main() -> int:
    default_work = Path("C:/gowport") if os.name == "nt" else Path("/tmp/gowport")
    parser = argparse.ArgumentParser(
        description=(
            "Generate native C++ from a user's own God of War PS2 ELF and "
            "build a local Android ARM64 Debug APK. No game files are uploaded."
        )
    )
    parser.add_argument(
        "--elf",
        required=True,
        help="path to the user's extracted SCUS_973.99 ELF (outside this repository)",
    )
    parser.add_argument(
        "--work-dir",
        default=str(default_work),
        help="private ASCII-only recompiler workspace outside the repository",
    )
    parser.add_argument(
        "--generate-only",
        action="store_true",
        help="generate and stage private C++ but do not invoke Gradle",
    )
    parser.add_argument(
        "--plan-only",
        action="store_true",
        help="validate paths and print the exact local commands without executing them",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    plan = create_build_plan(root, Path(args.elf), Path(args.work_dir))
    _require_ignored_generated_dir(root, plan.generated_dir)

    print(f"Private ELF: {plan.elf}")
    print(f"Private workspace: {plan.work_dir}")
    print(f"Git-ignored C++ staging: {plan.generated_dir}")
    print("No ELF, generated C++, IRX, ISO, save or other game-derived data is uploaded.")
    print("$ " + shlex.join(plan.recompiler_command))
    if not args.generate_only:
        print("$ " + shlex.join(plan.gradle_command))

    if args.plan_only:
        print("Plan validated. No files were generated and no build was started.")
        return 0

    _run(plan.recompiler_command, root)
    source_count = _verify_generated_outputs(plan.generated_dir)
    print(f"Verified private generated output: {source_count} C++ translation units.")

    if args.generate_only:
        print("Native C++ generated and staged. Android APK build was intentionally skipped.")
        return 0

    _run(plan.gradle_command, root)
    if not plan.apk_path.is_file():
        raise RuntimeError(f"Gradle reported success, but APK is missing: {plan.apk_path}")

    digest = hashlib.sha256(plan.apk_path.read_bytes()).hexdigest()
    print("")
    print(f"APK: {plan.apk_path}")
    print(f"SHA-256: {digest}")
    print("This APK contains code generated locally from the ELF supplied by the user.")
    print("Test it only with the matching original disc image and keep the game data private.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(
            f"Build step failed with exit code {error.returncode}. "
            "Review the command output above.",
            file=sys.stderr,
        )
        raise SystemExit(error.returncode)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

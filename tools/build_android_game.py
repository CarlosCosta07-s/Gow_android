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
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from build_recompiler_host import validate_ps2_elf


@dataclass(frozen=True)
class BuildPlan:
    elf: Path
    work_dir: Path
    generated_dir: Path
    prepare_runtime_command: tuple[str, ...]
    recompiler_command: tuple[str, ...]
    gradle_command: tuple[str, ...]
    apk_path: Path


@dataclass(frozen=True)
class GeneratedOutputSummary:
    translation_units: int
    table_slots: int
    registered_functions: int
    entry_point: int


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
    if elf.name.upper() != "SCUS_973.99":
        raise ValueError(
            "This Android AOT profile is currently pinned to the NTSC-U "
            "SCUS_973.99 executable. PAL SCES_531.33 needs its own function map "
            "and recompilation template before it can be built safely."
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

    prepare_runtime_command = (
        python,
        str(generator),
        "--prepare-android-runtime",
        "--work-dir",
        str(work),
    )
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
        ("cmd", "/c", str(wrapper), "--no-daemon", "--max-workers=1", "assembleDebug")
        if use_windows
        else (str(wrapper), "--no-daemon", "--max-workers=1", "assembleDebug")
    )
    return BuildPlan(
        elf=elf,
        work_dir=work,
        generated_dir=generated,
        prepare_runtime_command=prepare_runtime_command,
        recompiler_command=recompiler_command,
        gradle_command=gradle_command,
        apk_path=root / "android/app/build/outputs/apk/debug/app-debug.apk",
    )


def _run(
    command: tuple[str, ...],
    cwd: Path,
    *,
    environment: dict[str, str] | None = None,
) -> None:
    print("$ " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=str(cwd), check=True, env=environment)


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


def _verify_generated_outputs(
    generated_dir: Path,
    expected_entry_point: int,
) -> GeneratedOutputSummary:
    """Reject stale/empty AOT output before spending time compiling the APK."""
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

    registration_source = (generated_dir / "register_functions.cpp").read_text(
        encoding="utf-8"
    )

    def read_single(pattern: str, description: str, *, base: int = 10) -> int:
        matches = re.findall(pattern, registration_source)
        if len(matches) != 1:
            raise RuntimeError(
                f"Generated AOT registration must contain exactly one {description}; "
                f"found {len(matches)}."
            )
        return int(matches[0], base)

    table_base = read_single(
        r"g_ps2RecompiledFunctionTableBase\s*=\s*0x([0-9a-fA-F]+)u\s*;",
        "function-table base",
        base=16,
    )
    table_end = read_single(
        r"g_ps2RecompiledFunctionTableEnd\s*=\s*0x([0-9a-fA-F]+)u\s*;",
        "function-table end",
        base=16,
    )
    table_slots = read_single(
        r"g_ps2RecompiledFunctionTableSlotCount\s*=\s*(\d+)u\s*;",
        "function-table slot count",
    )

    if table_slots == 0:
        raise RuntimeError(
            "Generated AOT function table is empty (zero slots). "
            "The Android APK must not be built from an empty registration table."
        )
    if table_end <= table_base or (table_end - table_base) % 4 != 0:
        raise RuntimeError(
            "Generated AOT function-table address range is invalid or unaligned."
        )
    if (table_end - table_base) // 4 != table_slots:
        raise RuntimeError(
            "Generated AOT function-table slot count does not match its address range."
        )

    assignments = re.findall(
        r"(?m)^\s*g_ps2RecompiledFunctionTable\[(\d+)\]\s*=\s*"
        r"([A-Za-z_][A-Za-z0-9_:]*)\s*;\s*//\s*0x([0-9a-fA-F]+)\s*$",
        registration_source,
    )
    if not assignments:
        raise RuntimeError(
            "Generated AOT table declares slots but registers no native functions."
        )

    registered_addresses: set[int] = set()
    registered_slots: set[int] = set()
    for slot_text, function_name, address_text in assignments:
        del function_name  # The non-empty identifier is validated by the regex.
        slot = int(slot_text, 10)
        address = int(address_text, 16)
        if address & 3:
            raise RuntimeError(
                f"Generated AOT function address 0x{address:08X} is not 4-byte aligned."
            )
        if address < table_base or address >= table_end:
            raise RuntimeError(
                f"Generated AOT function 0x{address:08X} is outside the declared table range."
            )
        expected_slot = (address - table_base) // 4
        if slot != expected_slot or slot >= table_slots:
            raise RuntimeError(
                f"Generated AOT table maps 0x{address:08X} to slot {slot}, "
                f"but slot {expected_slot} is required."
            )
        if address in registered_addresses or slot in registered_slots:
            raise RuntimeError(
                f"Generated AOT table contains a duplicate registration for 0x{address:08X}."
            )
        registered_addresses.add(address)
        registered_slots.add(slot)

    if expected_entry_point not in registered_addresses:
        raise RuntimeError(
            "Generated AOT table does not cover the ELF entry point "
            f"0x{expected_entry_point:08X}. The runtime would refuse to start guest code."
        )

    return GeneratedOutputSummary(
        translation_units=len(function_cpp) + 1,
        table_slots=table_slots,
        registered_functions=len(assignments),
        entry_point=expected_entry_point,
    )


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
    build_mode = parser.add_mutually_exclusive_group(required=True)
    build_mode.add_argument(
        "--capture-bootstrap",
        action="store_true",
        help="build an explicit diagnostic APK that captures VU1/IOP code and stops on missing native AOT",
    )
    build_mode.add_argument(
        "--aot-captures",
        help="ZIP exported from GowGameActivity containing private vu1/ and iop/ captures for a full native build",
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
    build_root = (root / "android/app/build").resolve()
    generated_vu1 = build_root / "gow-generated-vu1"
    generated_iop = build_root / "gow-generated-iop"
    for generated_path in (generated_vu1, generated_iop):
        if generated_path.exists() and generated_path.is_symlink():
            raise ValueError(f"Generated staging may not be a symlink: {generated_path}")
        _require_ignored_generated_dir(root, generated_path)
    elf_info = validate_ps2_elf(plan.elf)

    captures_zip: Path | None = None
    if args.aot_captures:
        captures_zip = Path(args.aot_captures).expanduser().resolve()
        if not captures_zip.is_file():
            raise ValueError(f"Private AOT capture ZIP was not found: {captures_zip}")
        if _is_within(captures_zip, root):
            raise ValueError("Keep the user's AOT capture ZIP outside the public repository.")
        if not str(captures_zip).isascii():
            raise ValueError("The private AOT capture ZIP path must use ASCII characters.")

    print(f"Private ELF: {plan.elf}")
    print(f"Validated ELF entry point: 0x{elf_info.entry_point:08X}")
    print(f"Private workspace: {plan.work_dir}")
    print(f"Git-ignored EE C++ staging: {plan.generated_dir}")
    print("Mode: " + ("capture bootstrap (strict stop on missing VU1/IOP AOT)" if args.capture_bootstrap
                      else "full static AOT (EE + VU1 + IOP)"))
    print("No ELF, generated C++, IRX, microcode, ISO, save or other game-derived data is uploaded.")
    print("$ " + shlex.join(plan.prepare_runtime_command))
    print("$ " + shlex.join(plan.recompiler_command))
    if captures_zip is not None:
        native_aot_command = (
            sys.executable,
            str(root / "tools/build_android_native_aot.py"),
            "--captures-zip",
            str(captures_zip),
            "--work-dir",
            str(plan.work_dir / "native-aot"),
            "--output-root",
            str(build_root),
        )
        print("$ " + shlex.join(native_aot_command))
    else:
        native_aot_command = None
    if not args.generate_only:
        print("$ " + shlex.join(plan.gradle_command))

    if args.plan_only:
        print("Plan validated. No private files were read and no build was started.")
        return 0

    _run(plan.prepare_runtime_command, root)
    _run(plan.recompiler_command, root)
    generated = _verify_generated_outputs(
        plan.generated_dir,
        expected_entry_point=elf_info.entry_point,
    )
    print(
        "Verified private EE AOT output: "
        f"{generated.translation_units} C++ translation units; "
        f"{generated.registered_functions} registered functions; "
        f"{generated.table_slots} table slots; "
        f"ELF entry 0x{generated.entry_point:08X} is covered."
    )

    if native_aot_command is not None:
        _run(native_aot_command, root)
    else:
        for generated_path in (generated_vu1, generated_iop):
            if generated_path.exists():
                import shutil
                shutil.rmtree(generated_path)
        print(
            "Capture-bootstrap build: VU1/IOP generated AOT is intentionally absent. "
            "The runtime will capture loaded microcode/modules and stop on the first missing native block."
        )

    if args.generate_only:
        print("Native C++ generated and staged. Android APK build was intentionally skipped.")
        return 0

    gradle_environment = os.environ.copy()
    gradle_environment["GOW_ANDROID_CAPTURE_BOOTSTRAP"] = (
        "1" if args.capture_bootstrap else "0"
    )
    _run(plan.gradle_command, root, environment=gradle_environment)
    if not plan.apk_path.is_file():
        raise RuntimeError(f"Gradle reported success, but APK is missing: {plan.apk_path}")

    hasher = hashlib.sha256()
    with plan.apk_path.open("rb") as apk_stream:
        for chunk in iter(lambda: apk_stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    digest = hasher.hexdigest()
    print("")
    print(f"APK: {plan.apk_path}")
    print(f"SHA-256: {digest}")
    if args.capture_bootstrap:
        print("This is a capture-bootstrap APK, not a gameplay build.")
        print("Start the strict native guest once, export the VU1/IOP capture ZIP from the app, then rebuild with --aot-captures.")
    else:
        print("This APK contains private EE, VU1 and IOP code generated locally from the user's own captures.")
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

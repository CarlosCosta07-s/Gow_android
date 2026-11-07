#!/usr/bin/env python3
"""Build a local Android ARM64 APK from the user's original PS2 ISO.

The ISO is the only game input. SYSTEM.CNF, the NTSC-U ELF, IRX modules and disc
layout are extracted automatically into a private host workspace. Generated
C++ stays under Git-ignored android/app/build, and later VU1/IOP captures are
synchronized from the Android app's private files over ADB. Nothing game-derived
is uploaded or committed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from build_recompiler_host import validate_ps2_elf
from sync_android_aot_captures import (
    DEFAULT_PACKAGE,
    NoDeviceCaptures,
    pull_private_aot_captures,
    pull_private_installed_file,
    resolve_adb_device,
)
from migrate_android_debug_installation import migrate_debug_installation


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
    gradle_executable: str | None = None,
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
    gradle = gradle_executable or ("gradle.bat" if use_windows else "gradle")
    android_project = (root / "android").resolve()
    gradle_arguments = (
        "--project-dir",
        str(android_project),
        "--no-daemon",
        "--max-workers=1",
        "assembleDebug",
    )
    if use_windows and gradle.lower().endswith((".bat", ".cmd")):
        # CALL lets cmd.exe handle quoted paths (for example under Program Files)
        # without losing the batch file's argument boundaries.
        gradle_command = ("cmd", "/c", "call", gradle, *gradle_arguments)
    else:
        gradle_command = (gradle, *gradle_arguments)
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



def parse_system_cnf_boot_path(system_cnf: str) -> str:
    """Return the BOOT2 path from an ISO9660 SYSTEM.CNF file."""
    match = re.search(
        r"(?im)^\s*BOOT2\s*=\s*cdrom0\s*:\s*\\?([^\r\n;]+)",
        system_cnf,
    )
    if match is None:
        raise ValueError("The ISO SYSTEM.CNF does not contain a supported BOOT2 entry.")
    value = match.group(1).strip().replace("\\", "/")
    parts = [part for part in value.split("/") if part]
    if not parts or any(part in (".", "..") for part in parts):
        raise ValueError("The ISO SYSTEM.CNF contains an unsafe BOOT2 path.")
    return "/".join(parts)


def locate_ntsc_u_elf(extracted_disc: Path) -> Path:
    """Resolve the supported ELF only from the ISO-extracted private disc tree."""
    source_root = extracted_disc.expanduser()
    if source_root.is_symlink():
        raise ValueError("Private ISO extraction directory may not be a symbolic link.")
    root = source_root.resolve()
    if not root.is_dir():
        raise ValueError(f"Private ISO extraction directory is missing or unsafe: {root}")

    system_cnf_candidates = [
        item for item in root.iterdir()
        if item.is_file() and not item.is_symlink() and item.name.casefold() == "system.cnf"
    ]
    if len(system_cnf_candidates) != 1:
        raise ValueError("The extracted ISO must contain exactly one root SYSTEM.CNF.")
    boot_path = parse_system_cnf_boot_path(
        system_cnf_candidates[0].read_text(encoding="ascii", errors="replace")
    )
    boot_name = Path(boot_path).name.upper()
    if boot_name != "SCUS_973.99":
        raise ValueError(
            "This AOT profile only supports NTSC-U SCUS_973.99; "
            f"SYSTEM.CNF points to {boot_name or 'an unknown executable'}."
        )

    candidate = root.joinpath(*boot_path.split("/"))
    if not candidate.is_file():
        matches = [
            item for item in root.rglob("*")
            if item.is_file() and item.name.casefold() == "scus_973.99"
        ]
        if len(matches) != 1:
            raise ValueError(
                "The ISO extractor did not produce exactly one SCUS_973.99 "
                "at the path declared by SYSTEM.CNF."
            )
        candidate = matches[0]

    resolved = candidate.resolve()
    if not _is_within(resolved, root) or candidate.is_symlink():
        raise ValueError("The extracted BOOT2 ELF escapes the private disc workspace.")
    return resolved


def extract_iso_elf(
    iso_path: Path,
    work_dir: Path,
    repo_root: Path,
    *,
    python_executable: str | None = None,
) -> Path:
    """Extract a user's ISO with the pinned KIexster ISO tool; no manual ELF export."""
    iso = iso_path.expanduser().resolve()
    work = work_dir.expanduser().resolve()
    root = repo_root.expanduser().resolve()
    if not iso.is_file():
        raise ValueError(f"Original God of War ISO was not found: {iso}")
    if _is_within(iso, root):
        raise ValueError("Keep the original ISO outside the public Git repository.")
    if _is_within(work, root):
        raise ValueError("The private ISO workspace must be outside the Git repository.")
    if not str(work).isascii():
        raise ValueError("The private workspace path must use ASCII characters.")

    extractor = work / "sources" / "KIexter" / "tools" / "iso_extract.py"
    if not extractor.is_file() or extractor.is_symlink():
        raise RuntimeError(
            "The pinned KIexster ISO extractor is unavailable. "
            "Run the Android runtime preparation step before extracting the ISO."
        )

    destination = work / "iso-extracted"
    if destination.exists():
        if destination.is_symlink() or not _is_within(destination.resolve(), work):
            raise ValueError("Refusing to replace an unsafe private ISO extraction directory.")
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)

    command = (
        python_executable or sys.executable,
        "-I",
        str(extractor),
        str(iso),
        str(destination),
        "--layout",
    )
    print("Extracting the original ISO privately; ELF, IRX and disc layout are internal build inputs.")
    print("$ " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=str(extractor.parents[1]), check=True)

    for required in (".disc_layout", ".disc_meta"):
        if not (destination / required).is_file():
            raise RuntimeError(f"The ISO extraction did not produce {required}.")
    return locate_ntsc_u_elf(destination)


def install_and_launch_android_app(
    apk_path: Path,
    repo_root: Path,
    *,
    device_serial: str | None = None,
    install: bool = True,
) -> None:
    """Install the just-built Debug APK (when requested) and open the FORGE launcher."""
    if apk_path.is_symlink() or not apk_path.is_file() or apk_path.stat().st_size == 0:
        raise RuntimeError("Cannot install an APK that is missing, empty or a symbolic link.")

    adb, serial = resolve_adb_device(device_serial=device_serial)
    if install:
        _run((adb, "-s", serial, "install", "-r", str(apk_path)), repo_root)
    _run((adb, "-s", serial, "shell", "am", "force-stop", DEFAULT_PACKAGE), repo_root)
    _run((
        adb, "-s", serial, "shell", "am", "start",
        "-n", f"{DEFAULT_PACKAGE}/org.gowandroid.app.MainActivity",
    ), repo_root)
    print(
        "FORGE launcher started on Android. The private installation remains on the device; "
        "open INSTALAR JOGO there to enter the native game activity."
    )


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_full_native_aot_summary(
    summary_path: Path,
    expected_elf_path: Path,
) -> dict[str, object]:
    """Fail closed unless private VU1 and IOP native AOT output is complete."""
    if not summary_path.is_file() or summary_path.is_symlink():
        raise RuntimeError(
            "A complete native AOT summary is required before building a test candidate."
        )
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError("The private native AOT summary is unreadable.") from error

    if not isinstance(summary, dict) or summary.get("format") != "gow-native-aot-build-v1":
        raise RuntimeError("The private native AOT summary has an unsupported format.")
    elf = expected_elf_path.expanduser().resolve()
    if expected_elf_path.is_symlink() or not elf.is_file():
        raise RuntimeError("The expected private NTSC-U BOOT2 ELF is missing or unsafe.")
    game_identity = summary.get("game_identity")
    if not isinstance(game_identity, dict):
        raise RuntimeError(
            "The private AOT report is not bound to a BOOT2 ELF identity captured by Android."
        )
    if (
        game_identity.get("format") != "gow-android-game-identity-v1"
        or game_identity.get("installation_id") != "gow_scus_973_99_ntsc_u"
        or not isinstance(game_identity.get("boot_path"), str)
        or Path(game_identity["boot_path"]).name.upper() != "SCUS_973.99"
        or game_identity.get("elf_size") != elf.stat().st_size
        or not isinstance(game_identity.get("elf_sha256"), str)
        or not re.fullmatch(r"[0-9a-f]{64}", game_identity["elf_sha256"])
    ):
        raise RuntimeError("The private AOT report has an invalid or mismatched game identity.")
    if _sha256_file(elf) != game_identity["elf_sha256"]:
        raise RuntimeError(
            "The private AOT captures belong to a different BOOT2 ELF/ISO revision. "
            "Refusing to compile a test APK."
        )

    sources = summary.get("sources")
    if not isinstance(sources, dict):
        raise RuntimeError("The private native AOT summary has no source inventory.")
    vu1_sources = sources.get("vu1")
    iop_sources = sources.get("iop")
    if not isinstance(vu1_sources, list) or not isinstance(iop_sources, list):
        raise RuntimeError("The private native AOT source inventory is incomplete.")

    generated_vu1_dir = summary_path.parent / "gow-generated-vu1"
    generated_iop_dir = summary_path.parent / "gow-generated-iop"
    for directory in (generated_vu1_dir, generated_iop_dir):
        if directory.is_symlink() or not directory.is_dir():
            raise RuntimeError("A private generated AOT source directory is missing or unsafe.")

    def verify_generated_sources(names: list[object], directory: Path, label: str) -> None:
        if not names:
            raise RuntimeError(f"The private {label} AOT source inventory is empty.")
        seen_names: set[str] = set()
        for name in names:
            if (
                not isinstance(name, str)
                or Path(name).name != name
                or not name.endswith(".cpp")
                or name in seen_names
            ):
                raise RuntimeError(f"The private {label} AOT source inventory contains an unsafe or duplicate filename.")
            seen_names.add(name)
            source = directory / name
            if source.is_symlink() or not source.is_file() or source.stat().st_size <= 0:
                raise RuntimeError(f"The private {label} AOT source is missing, empty or unsafe: {name}")

    verify_generated_sources(vu1_sources, generated_vu1_dir, "VU1")
    verify_generated_sources(iop_sources, generated_iop_dir, "IOP")

    numeric_fields = (
        "vu1_micro_memories",
        "vu1_native_translation_units",
        "iop_modules",
        "iop_native_translation_units",
        "vu1_unsupported_blocks",
        "vu1_interpreter_callouts",
    )
    for field in numeric_fields:
        if not isinstance(summary.get(field), int) or summary[field] < 0:
            raise RuntimeError(f"The private native AOT summary has an invalid {field} value.")

    complete = (
        summary.get("full_native_aot") is True
        and summary["vu1_micro_memories"] > 0
        and summary["vu1_native_translation_units"] == 6
        and len(vu1_sources) == 6
        and summary["iop_modules"] > 0
        and summary["iop_native_translation_units"] == summary["iop_modules"]
        and len(iop_sources) == summary["iop_modules"] + 1
        and summary["vu1_unsupported_blocks"] == 0
        and summary["vu1_interpreter_callouts"] == 0
    )
    if not complete:
        raise RuntimeError(
            "Refusing to build a test APK: native AOT coverage is incomplete. "
            "Require six VU1 translation units, all captured IOP modules plus registry, "
            "zero unsupported VU1 blocks, and zero interpreter callouts."
        )
    return summary


def main() -> int:
    default_work = Path("C:/gowport") if os.name == "nt" else Path("/tmp/gowport")
    parser = argparse.ArgumentParser(
        description=(
            "Build an Android ARM64 God of War APK from the user's original ISO. "
            "The ISO is the only game input; the ELF and disc files are extracted privately."
        )
    )
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument(
        "--iso",
        help="path to the user's original NTSC-U God of War ISO (the only required game input)",
    )
    source_group.add_argument(
        "--elf",
        help=argparse.SUPPRESS,
    )
    source_group.add_argument(
        "--installed-android",
        action="store_true",
        help=(
            "read SYSTEM.CNF and BOOT2 directly from an existing private NTSC-U "
            "installation on one connected Android Debug app via ADB"
        ),
    )
    parser.add_argument(
        "--work-dir",
        default=str(default_work),
        help="private ASCII-only build workspace outside the repository",
    )
    parser.add_argument(
        "--device-serial",
        default=None,
        help="ADB serial when more than one authorized Android device is connected",
    )
    parser.add_argument(
        "--migrate-installed-debug-signature",
        action="store_true",
        help=(
            "before replacing a different-signature Debug APK, back up and verify the private disc, "
            "then restore the installation under the new signing key"
        ),
    )
    parser.add_argument(
        "--install-and-launch",
        action="store_true",
        help=(
            "after building, install/update the Debug APK on one authorized Android device "
            "and open the FORGE launcher; use --migrate-installed-debug-signature when the signing key changed"
        ),
    )
    # Private ADB synchronization supplies this archive automatically. The
    # hidden option remains only for internal tests and offline maintainer use.
    parser.add_argument(
        "--aot-captures",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--generate-only",
        action="store_true",
        help="generate and stage private C++ but do not invoke Gradle",
    )
    parser.add_argument(
        "--require-full-native-aot",
        action="store_true",
        help=(
            "refuse to compile an APK unless the private VU1/IOP AOT summary is complete; "
            "this is necessary but not sufficient to certify a playable build"
        ),
    )
    parser.add_argument(
        "--plan-only",
        action="store_true",
        help="validate the ISO/build paths and print the extraction/build plan without reading the ISO",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    work = Path(args.work_dir).expanduser().resolve()
    generator = root / "tools/build_recompiler_host.py"
    if _is_within(work, root):
        parser.error("The private workspace must be outside the public Git repository.")
    if not str(work).isascii():
        parser.error("Use an ASCII-only --work-dir, for example C:/gowport or /tmp/gowport.")
    if not generator.is_file():
        parser.error(f"Host recompiler entry point is missing: {generator}")

    iso_path: Path | None = None
    elf_path: Path | None = None
    if args.iso:
        iso_path = Path(args.iso).expanduser().resolve()
        if not iso_path.is_file():
            parser.error(f"Original God of War ISO not found: {iso_path}")
        if _is_within(iso_path, root):
            parser.error("Keep the original ISO outside the public Git repository.")
    elif args.elf:
        elf_path = Path(args.elf).expanduser().resolve()
    else:
        elf_path = None

    captures_zip: Path | None = None
    if args.aot_captures:
        captures_zip = Path(args.aot_captures).expanduser().resolve()
        if not captures_zip.is_file():
            parser.error(f"Private AOT capture archive was not found: {captures_zip}")
        if _is_within(captures_zip, root):
            parser.error("Keep private AOT captures outside the public Git repository.")
        if not str(captures_zip).isascii():
            parser.error("The private AOT capture archive path must use ASCII characters.")

    build_root = (root / "android/app/build").resolve()
    generated_dir = build_root / "gow-generated-game"
    generated_vu1 = build_root / "gow-generated-vu1"
    generated_iop = build_root / "gow-generated-iop"
    for generated_path in (generated_dir, generated_vu1, generated_iop):
        if generated_path.exists() and generated_path.is_symlink():
            parser.error(f"Generated staging may not be a symlink: {generated_path}")
        _require_ignored_generated_dir(root, generated_path)

    gradle_executable = shutil.which("gradle")
    if gradle_executable is None and os.name == "nt":
        gradle_executable = shutil.which("gradle.bat")
    if gradle_executable is None:
        gradle_executable = "gradle.bat" if os.name == "nt" else "gradle"
        if not (args.plan_only or args.generate_only):
            parser.error(
                "Gradle 9.6 was not found on PATH. Install Gradle 9.6 before building the Android APK. "
                "The host CI uses gradle/actions/setup-gradle@v4."
            )

    prepare_command = (
        sys.executable,
        str(generator),
        "--prepare-android-runtime",
        "--work-dir",
        str(work),
    )

    if args.install_and_launch and (args.plan_only or args.generate_only):
        parser.error("--install-and-launch requires a completed APK build, not --plan-only or --generate-only.")

    if args.plan_only and iso_path is not None:
        print(f"Original ISO input: {iso_path}")
        print(f"Private workspace: {work}")
        print(f"Automatic extraction target: {work / 'iso-extracted'}")
        print(f"Expected boot executable: {work / 'iso-extracted' / 'SCUS_973.99'}")
        print("Automatic intermediate inputs: SYSTEM.CNF, ELF, IRX modules, PAK files and original-sector layout.")
        print("The first Android APK is a strict AOT capture bootstrap unless private captures already exist.")
        print("$ " + shlex.join(prepare_command))
        print("Plan validated. No ISO contents were read and no private game data was generated.")
        return 0
    if args.plan_only and args.installed_android:
        print("Source: existing private NTSC-U installation on an authorized Android Debug app.")
        print(f"Private workspace: {work}")
        print("Inputs read from the device: SYSTEM.CNF and its BOOT2 executable.")
        print("The ISO, ELF and capture files are not exported manually.")
        print("$ " + shlex.join(prepare_command))
        print("Plan validated. No Android files were read and no private game data was generated.")
        return 0

    prepared_runtime = False
    if iso_path is not None:
        print(f"Original ISO input: {iso_path}")
        print(f"Private workspace: {work}")
        print("The ISO is the only game file supplied to this build.")
        _run(prepare_command, root)
        prepared_runtime = True
        elf_path = extract_iso_elf(iso_path, work, root)
        print(f"Automatically extracted private ELF: {elf_path}")
        if captures_zip is None:
            try:
                captures_zip = pull_private_aot_captures(
                    work,
                    root,
                    device_serial=args.device_serial,
                )
                print("Private VU1/IOP captures were synchronized automatically; no manual export is required.")
            except NoDeviceCaptures as error:
                print(
                    "No private Android AOT captures are available yet; "
                    "continuing with the first strict capture bootstrap. "
                    f"Details: {error}"
                )
    elif args.installed_android:
        print("Source: existing private Android NTSC-U installation.")
        print(f"Private workspace: {work}")
        adb_executable, selected_device = resolve_adb_device(
            device_serial=args.device_serial,
        )
        system_cnf_path = pull_private_installed_file(
            work,
            root,
            "SYSTEM.CNF",
            adb_executable=adb_executable,
            device_serial=selected_device,
            package_name=DEFAULT_PACKAGE,
            max_bytes=64 * 1024,
        )
        boot_relative_path = parse_system_cnf_boot_path(
            system_cnf_path.read_text(encoding="ascii", errors="replace")
        )
        if Path(boot_relative_path).name.upper() != "SCUS_973.99":
            raise RuntimeError(
                "The installed Android disc is not the supported NTSC-U SCUS_973.99 profile: "
                + boot_relative_path
            )
        elf_path = pull_private_installed_file(
            work,
            root,
            boot_relative_path,
            adb_executable=adb_executable,
            device_serial=selected_device,
            package_name=DEFAULT_PACKAGE,
        )
        print(f"Privately synchronized BOOT2 from Android: {boot_relative_path}")
        if captures_zip is None:
            try:
                captures_zip = pull_private_aot_captures(
                    work,
                    root,
                    adb_executable=adb_executable,
                    device_serial=selected_device,
                    package_name=DEFAULT_PACKAGE,
                )
                print("Private VU1/IOP captures were synchronized automatically.")
            except NoDeviceCaptures as error:
                print(
                    "No private Android AOT captures are available yet; "
                    "continuing with the first strict capture bootstrap. "
                    f"Details: {error}"
                )
    elif elf_path is not None and args.plan_only:
        plan = create_build_plan(
            root, elf_path, work, gradle_executable=gradle_executable
        )
        print("Private ELF: " + str(plan.elf))
        print("Plan validated. No private files were read and no build was started.")
        return 0

    if args.require_full_native_aot and captures_zip is None:
        raise RuntimeError(
            "No private VU1/IOP capture archive is available. Refusing to create a "
            "bootstrap APK as a test candidate; a first-boot capture build is diagnostic only."
        )

    assert elf_path is not None
    plan = create_build_plan(
        root, elf_path, work, gradle_executable=gradle_executable
    )
    _require_ignored_generated_dir(root, plan.generated_dir)

    elf_info = validate_ps2_elf(plan.elf)
    print(f"Validated PS2 ELF entry point: 0x{elf_info.entry_point:08X}")
    print(f"Private build workspace: {plan.work_dir}")
    print(f"Git-ignored EE C++ staging: {plan.generated_dir}")
    capture_bootstrap = captures_zip is None
    print("Build mode: " + (
        "automatic first-boot AOT capture bootstrap"
        if capture_bootstrap else "native AOT iteration from private captured VU1/IOP data"
    ))
    print("No ISO, ELF, IRX, PAK, microcode, generated C++ or save data is uploaded.")
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

    if not prepared_runtime:
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

    native_aot_complete = False
    if native_aot_command is not None:
        _run(native_aot_command, root)
        summary_path = build_root / "native-aot-summary.json"
        if not summary_path.is_file():
            raise RuntimeError("The private VU1/IOP AOT generator did not write its summary.")
        summary = (
            require_full_native_aot_summary(summary_path, plan.elf)
            if args.require_full_native_aot
            else json.loads(summary_path.read_text(encoding="utf-8"))
        )
        native_aot_complete = summary.get("full_native_aot") is True
        if not native_aot_complete:
            if args.require_full_native_aot:
                raise RuntimeError(
                    "Refusing to compile a test APK: the native AOT generator reported partial VU1/IOP coverage."
                )
            print(
                "Private VU1/IOP captures are partial: all currently available native blocks/modules "
                "will be linked; the runtime remains in strict capture mode for missing native code."
            )
    else:
        for generated_path in (generated_vu1, generated_iop):
            if generated_path.exists():
                shutil.rmtree(generated_path)
        print(
            "First-build mode: VU1/IOP generated AOT is intentionally absent. "
            "The Android runtime will retain private captures and stop at the first missing native block/module."
        )

    if args.generate_only:
        print("Private native C++ generated and staged. Android APK build was intentionally skipped.")
        return 0

    gradle_environment = os.environ.copy()
    gradle_environment["GOW_ANDROID_CAPTURE_BOOTSTRAP"] = (
        "0" if native_aot_complete else "1"
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
    if not native_aot_complete:
        print("This is a strict AOT capture/iteration APK, not a completed gameplay build.")
        print("Captures remain in the app's private files directory; they are not a user-facing export.")
    else:
        print("All private AOT captures supplied to this build were compiled; later game stages may request more code.")

    if args.migrate_installed_debug_signature:
        print("")
        print("Explicit one-time Debug signing migration requested.")
        migrate_debug_installation(
            apk_path=plan.apk_path,
            work_dir=work,
            repo_root=root,
            device_serial=args.device_serial,
        )
        if args.install_and_launch:
            install_and_launch_android_app(
                plan.apk_path, root,
                device_serial=args.device_serial,
                install=False,
            )
    elif args.install_and_launch:
        install_and_launch_android_app(
            plan.apk_path, root,
            device_serial=args.device_serial,
            install=True,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

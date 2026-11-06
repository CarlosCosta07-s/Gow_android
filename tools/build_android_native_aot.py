#!/usr/bin/env python3
"""Generate private VU1 and IOP native C++ from an Android AOT capture archive.

The archive is exported by GowGameActivity from the user's own installation.
Inputs and generated files must stay outside tracked source; generated C++ is
staged only under android/app/build, which is Git-ignored.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath


MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_FILES = 8192
VU1_MICRO_MEMORY_BYTES = 16 * 1024
IOP_NAME = re.compile(r"^[a-z0-9_]+$")
IOP_INTEGER = re.compile(r"^[0-9a-fA-F]+$")


def is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def reject_symlink_components(path: Path) -> None:
    current = path
    while current != current.parent:
        if current.is_symlink():
            raise ValueError(f"Symbolic links are not accepted in private AOT paths: {current}")
        current = current.parent


def fnv1a64(data: bytes) -> int:
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def safe_extract_archive(archive_path: Path, destination: Path) -> tuple[Path, Path]:
    reject_symlink_components(archive_path)
    if not archive_path.is_file():
        raise ValueError(f"AOT capture ZIP was not found: {archive_path}")
    if archive_path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("AOT capture ZIP exceeds the 512 MiB safety limit.")

    if destination.exists():
        if destination.is_symlink():
            raise ValueError("AOT extraction destination may not be a symlink.")
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)

    seen: set[str] = set()
    uncompressed = 0
    file_count = 0
    with zipfile.ZipFile(archive_path, "r") as archive:
        for info in archive.infolist():
            name = info.filename
            if not name or "\\" in name:
                raise ValueError(f"Invalid ZIP entry path: {name!r}")
            pure = PurePosixPath(name)
            if pure.is_absolute() or any(part in ("", ".", "..") for part in pure.parts):
                raise ValueError(f"Unsafe ZIP entry path: {name!r}")
            if name in seen:
                raise ValueError(f"Duplicate ZIP entry: {name}")
            seen.add(name)

            unix_mode = (info.external_attr >> 16) & 0xFFFF
            if unix_mode and (unix_mode & 0o170000) == 0o120000:
                raise ValueError(f"Symbolic-link ZIP entries are not accepted: {name}")

            if info.is_dir():
                continue
            file_count += 1
            uncompressed += info.file_size
            if file_count > MAX_ARCHIVE_FILES or uncompressed > MAX_ARCHIVE_BYTES:
                raise ValueError("AOT capture ZIP exceeds the file-count or size safety limit.")

            allowed = (
                len(pure.parts) == 2
                and pure.parts[0] in ("vu1", "iop")
                and pure.suffix == ".bin"
            ) or name == "iop/modules.txt" or name == "capture_manifest.txt"
            if not allowed:
                raise ValueError(f"Unexpected file in AOT capture ZIP: {name}")

            output = destination.joinpath(*pure.parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info, "r") as source, output.open("xb") as target:
                shutil.copyfileobj(source, target, length=1024 * 1024)

    vu1_dir = destination / "vu1"
    iop_dir = destination / "iop"
    # A first strict boot may stop before it has reached either subsystem.
    # Preserve partial captures so the next AOT iteration can advance further.
    vu1_dir.mkdir(parents=True, exist_ok=True)
    iop_dir.mkdir(parents=True, exist_ok=True)
    return vu1_dir, iop_dir


def validate_vu1_capture_directory(vu1_dir: Path) -> list[Path]:
    reject_symlink_components(vu1_dir)
    if not vu1_dir.is_dir():
        raise ValueError(f"VU1 capture directory is missing: {vu1_dir}")
    files = sorted(path for path in vu1_dir.glob("*.bin") if path.is_file())
    for path in files:
        if path.is_symlink():
            raise ValueError(f"VU1 capture may not be a symbolic link: {path}")
        if path.stat().st_size != VU1_MICRO_MEMORY_BYTES:
            raise ValueError(
                f"VU1 micro-memory {path.name} has {path.stat().st_size} bytes; "
                f"expected exactly {VU1_MICRO_MEMORY_BYTES}."
            )
    return files


def normalize_and_validate_iop_dump(iop_dir: Path, normalized_dir: Path) -> list[dict[str, object]]:
    reject_symlink_components(iop_dir)
    manifest = iop_dir / "modules.txt"
    if not iop_dir.is_dir():
        raise ValueError(f"IOP capture directory is missing: {iop_dir}")
    captured_bins = {
        path.stem for path in iop_dir.glob("*.bin")
        if path.is_file() and not path.is_symlink()
    }
    if not manifest.is_file():
        if captured_bins:
            raise ValueError("IOP .bin captures exist but iop/modules.txt is missing.")
        normalized_dir.mkdir(parents=True, exist_ok=True)
        return []

    latest: dict[tuple[str, int], tuple[int, int]] = {}
    name_bases: dict[str, int] = {}
    for line_number, line in enumerate(
        manifest.read_text(encoding="ascii").splitlines(), start=1
    ):
        fields = line.split()
        if not fields:
            continue
        if len(fields) != 4:
            raise ValueError(f"Malformed IOP modules.txt line {line_number}.")
        name, base_text, size_text, hash_text = fields
        if not IOP_NAME.fullmatch(name):
            raise ValueError(f"Invalid IOP module name on line {line_number}: {name!r}")
        if not all(IOP_INTEGER.fullmatch(part) for part in (base_text, size_text, hash_text)):
            raise ValueError(f"Invalid hexadecimal field on IOP line {line_number}.")
        base = int(base_text, 16)
        size = int(size_text, 16)
        expected_hash = int(hash_text, 16)
        if size <= 0 or size > 64 * 1024 * 1024 or size % 4 != 0:
            raise ValueError(f"Invalid IOP module text size on line {line_number}: {size}")
        if expected_hash > 0xFFFFFFFFFFFFFFFF:
            raise ValueError(f"IOP FNV hash overflows 64 bits on line {line_number}.")
        previous_base = name_bases.setdefault(name, base)
        if previous_base != base:
            raise ValueError(
                f"IOP module {name} was captured at multiple load bases; "
                "the current native registry format supports one base per module name."
            )
        latest[(name, base)] = (size, expected_hash)

    if not latest:
        if captured_bins:
            raise ValueError("IOP .bin captures exist but modules.txt contains no module records.")
        normalized_dir.mkdir(parents=True, exist_ok=True)
        return []
    if captured_bins != {name for name, _base in latest}:
        raise ValueError("IOP capture directory contains unlisted or missing module .bin files.")

    normalized_dir.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    manifest_lines: list[str] = []
    for (name, base), (size, expected_hash) in sorted(latest.items()):
        source = iop_dir / f"{name}.bin"
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"Captured IOP module file is missing or unsafe: {source.name}")
        data = source.read_bytes()
        if len(data) != size:
            raise ValueError(
                f"IOP module {name} size mismatch: modules.txt says {size}, "
                f"captured file contains {len(data)} bytes."
            )
        actual_hash = fnv1a64(data)
        if actual_hash != expected_hash:
            raise ValueError(
                f"IOP module {name} hash mismatch: modules.txt says "
                f"{expected_hash:016x}, file contains {actual_hash:016x}."
            )
        shutil.copy2(source, normalized_dir / source.name)
        manifest_lines.append(f"{name} {base:x} {size:x} {actual_hash:x}")
        records.append({
            "name": name,
            "base": base,
            "size": size,
            "fnv1a64": f"{actual_hash:016x}",
        })

    (normalized_dir / "modules.txt").write_text(
        "\n".join(manifest_lines) + "\n", encoding="ascii"
    )
    return records


def run_generator(command: list[str], *, cwd: Path) -> str:
    print("$ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        cwd=str(cwd),
        text=True,
        capture_output=True,
        check=False,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="" if result.stderr.endswith("\n") else "\n", flush=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"Native AOT generator failed with exit code {result.returncode}: {command[1]}"
        )
    return result.stdout


def generate_native_aot(
    repo_root: Path,
    captures_zip: Path,
    work_dir: Path,
    output_root: Path,
) -> dict[str, object]:
    root = repo_root.resolve()
    captures = captures_zip.expanduser().resolve()
    work = work_dir.expanduser().resolve()
    output = output_root.expanduser().resolve()
    build_root = (root / "android/app/build").resolve()

    if is_within(captures, root):
        raise ValueError("Keep the user's AOT capture ZIP outside the public repository.")
    if is_within(work, root):
        raise ValueError("The AOT work directory must stay outside the public repository.")
    if not is_within(output, build_root):
        raise ValueError("Generated AOT output must remain inside android/app/build.")
    reject_symlink_components(captures)
    reject_symlink_components(work)
    reject_symlink_components(output)

    extracted = work / "aot-capture-input"
    vu1_dir, iop_dir = safe_extract_archive(captures, extracted)
    vu1_files = validate_vu1_capture_directory(vu1_dir)

    normalized_iop = work / "aot-iop-normalized"
    if normalized_iop.exists():
        shutil.rmtree(normalized_iop)
    iop_records = normalize_and_validate_iop_dump(iop_dir, normalized_iop)

    vu1_generator = root / "port/kiexter/tools/vu1n_gen.py"
    iop_generator = root / "port/kiexter/tools/iopn_gen.py"
    heads_file = root / "port/kiexter/tools/vu1n_heads.txt"
    for tool in (vu1_generator, iop_generator):
        if not tool.is_file():
            raise RuntimeError(f"Required AOT generator is missing: {tool}")

    vu1_output = output / "gow-generated-vu1"
    iop_output = output / "gow-generated-iop"
    for folder in (vu1_output, iop_output):
        if folder.exists():
            if folder.is_symlink():
                raise ValueError(f"Generated AOT output may not be a symlink: {folder}")
            shutil.rmtree(folder)
        folder.mkdir(parents=True, exist_ok=True)

    python = sys.executable
    iop_registry = iop_output / "iopn_registry.cpp"
    iop_sources: list[Path] = []
    if iop_records:
        run_generator(
            [python, "-I", str(iop_generator), str(normalized_iop), str(iop_output)],
            cwd=root,
        )
        iop_sources = sorted(
            path for path in iop_output.glob("iopn_*.cpp")
            if path.name != "iopn_registry.cpp"
        )
        if not iop_registry.is_file() or len(iop_sources) != len(iop_records):
            raise RuntimeError(
                f"IOP AOT output mismatch: validated {len(iop_records)} modules, "
                f"generated {len(iop_sources)} module sources and registry="
                f"{iop_registry.is_file()}."
            )
        registry_text = iop_registry.read_text(encoding="utf-8")
        count_match = re.search(r"count\s*=\s*(\d+)\s*;", registry_text)
        if count_match is None or int(count_match.group(1)) != len(iop_records):
            raise RuntimeError("Generated IOP registry count does not match the captured module list.")
    elif iop_sources or iop_registry.exists():
        raise RuntimeError("Unexpected IOP generated sources exist without validated module captures.")

    vu1_sources: list[Path] = []
    if vu1_files:
        vu1_command = [
            python, "-I", str(vu1_generator),
            str(vu1_output), str(vu1_dir),
        ]
        if heads_file.is_file():
            vu1_command.extend(["--heads", str(heads_file)])
        vu1_command.extend(["--parts", "6", "--strict"])
        vu1_stdout = run_generator(vu1_command, cwd=root)
        vu1_sources = sorted(vu1_output.glob("vu1n_part*.cpp"))
        if len(vu1_sources) != 6:
            raise RuntimeError(
                f"VU1 AOT generator produced {len(vu1_sources)} parts; exactly 6 are required."
            )
        if "STRICT AOT COVERAGE PASSED" not in vu1_stdout:
            raise RuntimeError("VU1 generator did not confirm strict native-only block coverage.")
        for source in vu1_sources:
            if not source.is_file() or source.stat().st_size == 0:
                raise RuntimeError(f"Generated VU1 translation unit is empty: {source.name}")

    summary: dict[str, object] = {
        "format": "gow-native-aot-build-v1",
        "vu1_micro_memories": len(vu1_files),
        "vu1_native_translation_units": len(vu1_sources),
        "iop_modules": len(iop_records),
        "iop_native_translation_units": len(iop_sources),
        "full_native_aot": bool(vu1_sources and iop_records),
        "iop": iop_records,
        "sources": {
            "vu1": [path.name for path in vu1_sources],
            "iop": [path.name for path in iop_sources] + ([iop_registry.name] if iop_registry.is_file() else []),
        },
    }
    (output / "native-aot-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    state = "complete" if summary["full_native_aot"] else "partial; another strict capture iteration is required"
    print(
        f"Private native AOT: {state}; "
        f"{len(vu1_files)} VU1 micro-memories, "
        f"{len(iop_records)} IOP modules, "
        f"{len(vu1_sources)} VU1 parts and {len(iop_sources)} IOP module sources."
    )
    print("All generated game-derived code remains under android/app/build (Git-ignored).")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate an Android AOT capture ZIP and generate strict native VU1/IOP C++."
    )
    parser.add_argument("--captures-zip", required=True, help="ZIP exported from GowGameActivity.")
    parser.add_argument("--work-dir", required=True, help="Private ASCII-only work directory outside the repository.")
    parser.add_argument(
        "--output-root",
        default=None,
        help="Dedicated directory under android/app/build (default: android/app/build).",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_root) if args.output_root else root / "android/app/build"
    work = Path(args.work_dir).expanduser().resolve()
    generate_native_aot(root, Path(args.captures_zip), work, output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"Native AOT build command failed with exit code {error.returncode}.", file=sys.stderr)
        raise SystemExit(error.returncode)
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

#!/usr/bin/env python3
"""Build the KIexster-pinned PS2Recomp host tool and generate GoW C++ privately.

This script does not build the Android APK or the PS2 runtime. It only runs the
host-side static recompiler. Keep the ELF and generated C++ outside this repo.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PS2RECOMP_URL = "https://github.com/ran-j/PS2Recomp.git"
PS2RECOMP_COMMIT = "c5a9d02573410a2085a4b4b831b0b68ba3515440"
KIEXTER_URL = "https://github.com/KIexster/god-of-war-recomp.git"
KIEXTER_COMMIT = "a226151feb6ed22937e42be485aaf28a83acd166"
BASE_PATCH = "patches/ps2recomp-waffle-runtime.patch"


def run(command: list[str], *, cwd: Path | None = None) -> None:
    print("+ " + " ".join(str(part) for part in command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def checkout_pinned_repo(url: str, commit: str, destination: Path) -> None:
    if destination.exists():
        if not (destination / ".git").exists():
            raise RuntimeError(
                f"Refusing to overwrite a non-Git directory: {destination}"
            )
        run(["git", "remote", "set-url", "origin", url], cwd=destination)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.mkdir()
        run(["git", "init"], cwd=destination)
        run(["git", "remote", "add", "origin", url], cwd=destination)

    run(["git", "fetch", "--depth", "1", "origin", commit], cwd=destination)
    run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=destination)
    run(["git", "reset", "--hard", "FETCH_HEAD"], cwd=destination)
    run(["git", "clean", "-fdx"], cwd=destination)
    if (destination / ".gitmodules").is_file():
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=destination)


def toml_path(path: Path) -> str:
    value = path.resolve().as_posix()
    if any(character in value for character in ("\n", "\r")):
        raise ValueError("A path contains a line break and cannot be written to TOML.")
    return value.replace('"', '\\"')


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile PS2Recomp on the host and generate private GoW C++."
    )
    parser.add_argument(
        "--elf",
        default=os.environ.get("GOW_ELF"),
        help="path to the user's extracted SCUS_973.99 ELF (or set GOW_ELF)",
    )
    default_work = Path("C:/gowport") if os.name == "nt" else Path("/tmp/gowport")
    parser.add_argument(
        "--work-dir",
        default=os.environ.get("GOW_PRIVATE_WORKDIR", str(default_work)),
        help="private workspace outside the Git repository (default: C:/gowport or /tmp/gowport)",
    )
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="build the pinned host recompiler without requiring or reading a game ELF",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    elf: Path | None = None
    if not args.build_only:
        if not args.elf:
            parser.error("Provide --elf or set GOW_ELF to your own extracted SCUS_973.99 file.")
        elf = Path(args.elf).expanduser().resolve()
        if not elf.is_file():
            parser.error(f"ELF file not found: {elf}")

    work = Path(args.work_dir).expanduser().resolve()
    try:
        work.relative_to(repo_root)
    except ValueError:
        pass
    else:
        parser.error("The private workspace must be outside the public Git repository.")

    # The pinned recompiler has trouble opening paths with non-ASCII characters.
    if not str(work).isascii():
        parser.error(
            "Use an ASCII-only --work-dir, for example C:/gowport or /tmp/gowport."
        )

    funcmap = repo_root / "recompiler/config/funcmap.csv"
    template_path = repo_root / "recompiler/config/recomp.template.toml"
    if not args.build_only and (not funcmap.is_file() or not template_path.is_file()):
        parser.error("The imported function map or recompilation template is missing.")

    sources = work / "sources"
    recompiler_source = sources / "PS2Recomp"
    kiexter_source = sources / "KIexster"
    build_dir = work / "build-recompiler"
    input_dir = work / "input"
    generated_dir = work / "generated"

    work.mkdir(parents=True, exist_ok=True)
    print(f"Private workspace: {work}")
    if args.build_only:
        print("Mode: host-tool build only; no game files are read or generated.")
    else:
        print("Target profile: God of War PS2 NTSC-U / SCUS_973.99")
    print("This stage runs on the host; it does not build or modify the Android APK.")

    checkout_pinned_repo(KIEXTER_URL, KIEXTER_COMMIT, kiexter_source)
    checkout_pinned_repo(PS2RECOMP_URL, PS2RECOMP_COMMIT, recompiler_source)

    patch_path = kiexter_source / BASE_PATCH
    if not patch_path.is_file():
        raise RuntimeError(f"Required KIexster foundation patch not found: {patch_path}")
    run(
        ["git", "apply", "--check", "--ignore-whitespace", str(patch_path)],
        cwd=recompiler_source,
    )
    run(
        ["git", "apply", "--ignore-whitespace", str(patch_path)],
        cwd=recompiler_source,
    )

    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("CMake 3.21+ was not found on PATH.")
    version_line = subprocess.run(
        [cmake, "--version"], capture_output=True, text=True, check=True
    ).stdout.splitlines()[0]
    try:
        cmake_version = tuple(int(part) for part in version_line.split()[-1].split(".")[:2])
    except (IndexError, ValueError) as error:
        raise RuntimeError(f"Could not parse CMake version: {version_line}") from error
    if cmake_version < (3, 21):
        raise RuntimeError(
            f"CMake 3.21 or newer is required; found {version_line}."
        )
    generator = shutil.which("ninja")

    configure = [
        cmake,
        "-S", str(recompiler_source),
        "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DPS2X_BUILD_RECOMP=ON",
        "-DPS2X_BUILD_RUNTIME=OFF",
        "-DPS2X_BUILD_ANALYZER=OFF",
        "-DPS2X_BUILD_TEST=OFF",
        "-DPS2X_BUILD_STUDIO=OFF",
    ]
    if generator:
        configure.extend(["-G", "Ninja"])
    run(configure)
    run([cmake, "--build", str(build_dir), "--target", "ps2_recomp", "--parallel"])

    executable_names = {"ps2_recomp", "ps2_recomp.exe"}
    candidates = [
        path for path in build_dir.rglob("*")
        if path.is_file() and path.name in executable_names
    ]
    if not candidates:
        raise RuntimeError("The host build completed without producing ps2_recomp.")
    executable = candidates[0]
    if args.build_only:
        print("")
        print(f"Host recompiler built successfully: {executable}")
        print("No game ELF was accessed and no game-derived C++ was generated.")
        return 0

    assert elf is not None
    input_dir.mkdir(parents=True, exist_ok=True)
    private_elf = input_dir / "SCUS_973.99"
    private_map = input_dir / "funcmap.csv"
    shutil.copy2(elf, private_elf)
    shutil.copy2(funcmap, private_map)

    if generated_dir.exists():
        shutil.rmtree(generated_dir)
    generated_dir.mkdir(parents=True)

    config = template_path.read_text(encoding="utf-8")
    replacements = {
        "@ELF@": toml_path(private_elf),
        "@MAP@": toml_path(private_map),
        "@OUT@": toml_path(generated_dir) + "/",
    }
    for marker, value in replacements.items():
        if config.count(marker) != 1:
            raise RuntimeError(
                f"Expected exactly one {marker} marker in {template_path}."
            )
        config = config.replace(marker, value)

    config_path = work / "recomp-config.toml"
    config_path.write_text(config, encoding="ascii")
    run([str(executable), str(config_path)])

    generated_files = list(generated_dir.glob("*.cpp"))
    if not generated_files:
        raise RuntimeError(
            "PS2Recomp did not produce C++ files. Check the ELF, function map and config."
        )

    print("")
    print(f"Generated C++ files: {len(generated_files)}")
    print(f"Private output: {generated_dir}")
    print("Not done by this script: Android ARM64 compilation, runtime integration, VU1/IOP AOT, or gameplay testing.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"Command failed with exit code {error.returncode}.", file=sys.stderr)
        raise SystemExit(error.returncode)
    except Exception as error:  # Keep build failures readable for the user.
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

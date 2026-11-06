#!/usr/bin/env python3
"""Build the KIexster-pinned PS2Recomp host tool and generate GoW C++ privately.

This script does not build the Android APK or the PS2 runtime. It only runs the
host-side static recompiler. Keep the ELF and generated C++ outside this repo.
"""
from __future__ import annotations

import argparse
import json
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


def replace_exactly_once(path: Path, old: str, new: str, description: str) -> None:
    source = path.read_text(encoding="utf-8")
    matches = source.count(old)
    if matches != 1:
        raise RuntimeError(
            f"Expected one anchor for {description} in {path}, found {matches}."
        )
    path.write_text(source.replace(old, new, 1), encoding="utf-8")
    print(f"Applied Android runtime policy: {description}")


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

    # Strict IOP mode: dispatch known AOT IRX functions, but refuse the guest CPU fallback.
    iop_cpp = recompiler_source / "ps2xIOP/src/emulator/iop_emulator.cpp"
    replace_exactly_once(
        iop_cpp,
        "            ++interpretedInstructions;\n",
        """#if defined(PS2X_IOP_NATIVE_ONLY)
            std::ostringstream out;
            out << "[IOP:native-required] refusing fallback to interpreted IOP instruction"
                << " pc=0x" << std::hex << cpu.pc
                << " ra=0x" << cpu.gpr[31]
                << " branchPending=" << std::dec << (cpu.branchPending ? 1 : 0);
            log(LogLevel::Error, out.str());
            cpu.stopped = true;
            cpu.gpr[2] = 0xFFFFFFFFu;
            return false;
#else
            ++interpretedInstructions;
""",
        "IOP interpreted-instruction fallback guard",
    )
    replace_exactly_once(
        iop_cpp,
        "            return running;\n        }\n\n        uint32_t runCpu(CpuState &cpu, uint32_t instructionBudget)",
        "            return running;\n#endif\n        }\n\n        uint32_t runCpu(CpuState &cpu, uint32_t instructionBudget)",
        "IOP strict-mode preprocessor close",
    )

    # Keep exception entry semantics available to native AOT code without constructing IopCpuCore.
    replace_exactly_once(
        iop_cpp,
        """        bool iequals(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
                return false;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(rhs[i])))
                    return false;
            }
            return true;
        }

    }

    class IopEmulator::Impl""",
        """        bool iequals(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
                return false;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(rhs[i])))
                    return false;
            }
            return true;
        }

        void raiseIopException(IopCpuState &cpu, uint32_t code, uint32_t faultPc, bool delaySlot,
                               std::optional<uint32_t> badAddress = std::nullopt)
        {
            uint32_t cause = cpu.cop0[13] & ~0x7Cu;
            cause |= (code & 0x1Fu) << 2u;
            if (delaySlot)
            {
                cause |= 0x80000000u;
                cpu.cop0[14] = faultPc - 4u;
            }
            else
            {
                cause &= ~0x80000000u;
                cpu.cop0[14] = faultPc;
            }
            cpu.cop0[13] = cause;
            if (badAddress)
                cpu.cop0[8] = *badAddress;
            const uint32_t status = cpu.cop0[12];
            cpu.cop0[12] = (status & ~0x3Fu) | ((status << 2u) & 0x3Fu);
            cpu.pc = (status & (1u << 22u)) ? 0xBFC00180u : 0x80000080u;
            cpu.branchPending = false;
            cpu.pendingLoad = false;
            cpu.exception = true;
        }

    }

    class IopEmulator::Impl""",
        "standalone IOP exception entry helper",
    )
    replace_exactly_once(
        iop_cpp,
        "              ioman(memory),\n              cpuCore(memory),\n              imports(memory),",
        "              ioman(memory),\n#if !defined(PS2X_IOP_NATIVE_ONLY)\n              cpuCore(memory),\n#endif\n              imports(memory),",
        "conditional IOP interpreter construction",
    )
    replace_exactly_once(
        iop_cpp,
        "            cpuCore.raiseException(cpu, 0u, cpu.pc, false);",
        "            raiseIopException(cpu, 0u, cpu.pc, false);",
        "independent IOP exception call",
    )
    replace_exactly_once(
        iop_cpp,
        "        IopCpuCore cpuCore;\n",
        "#if !defined(PS2X_IOP_NATIVE_ONLY)\n        IopCpuCore cpuCore;\n#endif\n",
        "conditional IOP interpreter member",
    )

    iop_cmake = recompiler_source / "ps2xIOP/CMakeLists.txt"
    replace_exactly_once(
        iop_cmake,
        "    src/emulator/core/iop_cpu.cpp\n",
        "",
        "exclude IOP guest interpreter from unconditional source list",
    )
    replace_exactly_once(
        iop_cmake,
        "    src/modules/gow_stub_services.cpp\n)\n\n# GOW-Port: IRX modules compiled ahead of time",
        "    src/modules/gow_stub_services.cpp\n)\n\n"
        "if(NOT PS2X_IOP_NATIVE_ONLY)\n"
        "    target_sources(ps2_iop PRIVATE src/emulator/core/iop_cpu.cpp)\n"
        "endif()\n\n"
        "# GOW-Port: IRX modules compiled ahead of time",
        "conditionally compile IOP interpreter only in non-strict builds",
    )
    replace_exactly_once(
        iop_cmake,
        'option(PS2X_IOP_BUILD_TESTS "Build ps2xIOP emulator smoke tests" OFF)\n',
        'option(PS2X_IOP_BUILD_TESTS "Build ps2xIOP emulator smoke tests" OFF)\n'
        'option(PS2X_IOP_NATIVE_ONLY "Reject interpreted IOP instructions without AOT coverage" OFF)\n'
        'if(ANDROID)\n'
        '    set(PS2X_IOP_NATIVE_ONLY ON CACHE BOOL "" FORCE)\n'
        'endif()\n',
        "IOP native-only CMake option",
    )
    replace_exactly_once(
        iop_cmake,
        "target_compile_features(ps2_iop PUBLIC cxx_std_20)\n",
        "if(PS2X_IOP_NATIVE_ONLY)\n"
        "    target_compile_definitions(ps2_iop PRIVATE PS2X_IOP_NATIVE_ONLY=1)\n"
        "endif()\n\n"
        "target_compile_features(ps2_iop PUBLIC cxx_std_20)\n",
        "IOP native-only compile definition",
    )

    # Strict VU1 policy: never fall back to the cycle-exact interpreter.
    vu_core = recompiler_source / "ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp"
    once_vu = lambda old, new, label: replace_exactly_once(vu_core, old, new, label)

    once_vu(
        """        bool usedNative = false;
        for (;;)
        {
            if (runNative(ctx))
                return true;
            if (!m_native || !m_native->enabled)
                return usedNative;
            usedNative = true;
            if (!stepInterpreted(ctx))
                return true;
        }""",
        """#if !defined(PS2X_VU1_NATIVE_ONLY)
        bool usedNative = false;
#endif
        for (;;)
        {
            if (runNative(ctx))
                return true;
#if defined(PS2X_VU1_NATIVE_ONLY)
            uint32_t lower = 0u, upper = 0u;
            const uint32_t pc = m_state.pc & 0x3FFFu;
            if (vuCode != nullptr && pc + 8u <= codeSize)
            {
                std::memcpy(&lower, vuCode + pc, sizeof(lower));
                std::memcpy(&upper, vuCode + pc + 4u, sizeof(upper));
            }
            std::fprintf(stderr,
                         "[VU1:native-required] exact-engine fallback blocked: pc=0x%04x lower=0x%08x upper=0x%08x cycle=%llu ebit=%u haltD=%u haltT=%u\\n",
                         pc, lower, upper, static_cast<unsigned long long>(m_cycle),
                         m_state.ebit ? 1u : 0u, m_pendingHaltD ? 1u : 0u, m_pendingHaltT ? 1u : 0u);
            m_stopRequested = true;
            return true;
#else
            if (!m_native || !m_native->enabled)
                return usedNative;
            usedNative = true;
            if (!stepInterpreted(ctx))
                return true;
#endif
        }""",
        "VU1 native-only run-loop fallback guard",
    )
    once_vu(
        "    if (m_unit == Unit::VU1 && !m_native)\\n        m_native = new NativeState();",
        "    if (m_unit == Unit::VU1 && !m_native)\\n        m_native = new NativeState();\\n"
        "#if defined(PS2X_VU1_NATIVE_ONLY)\\n"
        "    if (m_unit == Unit::VU1 && m_native)\\n        m_native->enabled = true;\\n"
        "#endif",
        "force VU1 native dispatch enabled in strict builds",
    )
    once_vu(
        "    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)\\n"
        "        VU1NativeAccess::recordReplay(*this, &context, maxCycles);",
        "#if !defined(PS2X_VU1_NATIVE_ONLY)\\n"
        "    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)\\n"
        "        VU1NativeAccess::recordReplay(*this, &context, maxCycles);\\n"
        "#endif",
        "exclude VU1 interpreter record-replay from strict runtime",
    )
    once_vu(
        "    if (m_unit == Unit::VU1 && m_native && m_native->enabled && m_native->verifyLeft != 0u && vuData && codeSize == PS2_VU1_CODE_SIZE)\\n    {",
        "#if !defined(PS2X_VU1_NATIVE_ONLY)\\n"
        "    if (m_unit == Unit::VU1 && m_native && m_native->enabled && m_native->verifyLeft != 0u && vuData && codeSize == PS2_VU1_CODE_SIZE)\\n    {",
        "exclude VU1 interpreter A/B verifier from strict runtime",
    )
    once_vu(
        "        else if (checked % 2000u == 0u)\\n"
        "            std::fprintf(stderr, \"[vu1n-verify] %u runs checked, %u mismatches\\n\", checked, bad);\\n"
        "    }\\n"
        "    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)\\n"
        "    {\\n"
        "        if (!runNativeLoop(context))\\n"
        "            runExact(context);\\n"
        "    }\\n"
        "    else\\n"
        "        runExact(context);",
        "        else if (checked % 2000u == 0u)\\n"
        "            std::fprintf(stderr, \"[vu1n-verify] %u runs checked, %u mismatches\\n\", checked, bad);\\n"
        "    }\\n"
        "#endif\\n"
        "#if defined(PS2X_VU1_NATIVE_ONLY)\\n"
        "    if (m_unit == Unit::VU1)\\n"
        "    {\\n"
        "        if (vuData != nullptr && codeSize == PS2_VU1_CODE_SIZE)\\n"
        "            runNativeLoop(context);\\n"
        "        else\\n"
        "        {\\n"
        "            std::fprintf(stderr, \"[VU1:native-required] invalid VU1 execution buffer: codeSize=%u data=%p\\n\", codeSize, static_cast<void *>(vuData));\\n"
        "            m_stopRequested = true;\\n"
        "        }\\n"
        "    }\\n"
        "    else\\n"
        "        runExact(context);\\n"
        "#else\\n"
        "    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)\\n"
        "    {\\n"
        "        if (!runNativeLoop(context))\\n"
        "            runExact(context);\\n"
        "    }\\n"
        "    else\\n"
        "        runExact(context);\\n"
        "#endif",
        "strict VU1 runtime dispatch without interpreter fallback",
    )

    vu_cmake = recompiler_source / "ps2xRuntime/CMakeLists.txt"
    replace_exactly_once(
        vu_cmake,
        'option(PS2X_ENABLE_IOP_RPC_TRACE "Log unhandled IOP/SIF RPC trace suggestions" ON)\\n',
        'option(PS2X_ENABLE_IOP_RPC_TRACE "Log unhandled IOP/SIF RPC trace suggestions" ON)\\n'
        'option(PS2X_VU1_NATIVE_ONLY "Reject VU1 exact-engine fallback when AOT coverage is missing" OFF)\\n'
        'if(ANDROID)\\n'
        '    set(PS2X_VU1_NATIVE_ONLY ON CACHE BOOL "" FORCE)\\n'
        'endif()\\n',
        "VU1 native-only CMake option",
    )
    replace_exactly_once(
        vu_cmake,
        "target_compile_definitions(ps2_runtime PRIVATE\\n    PS2X_HAS_FFMPEG=$<BOOL:${PS2X_ENABLE_FFMPEG}>\\n)\\n",
        "if(PS2X_VU1_NATIVE_ONLY)\\n"
        "    target_compile_definitions(ps2_runtime PRIVATE PS2X_VU1_NATIVE_ONLY=1)\\n"
        "endif()\\n\\n"
        "target_compile_definitions(ps2_runtime PRIVATE\\n    PS2X_HAS_FFMPEG=$<BOOL:${PS2X_ENABLE_FFMPEG}>\\n)\\n",
        "VU1 native-only compile definition",
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

    # Compile the IOP core with strict AOT-only dispatch enabled. This validates the
    # refusal path on the host without requiring or generating any game modules.
    iop_source = recompiler_source / "ps2xIOP"
    iop_build_dir = work / "build-iop-native-only"
    if not (iop_source / "CMakeLists.txt").is_file():
        raise RuntimeError(f"Waffle IOP source tree is missing: {iop_source}")
    run([
        cmake,
        "-S", str(iop_source),
        "-B", str(iop_build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DPS2X_IOP_NATIVE_ONLY=ON",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ])
    run([cmake, "--build", str(iop_build_dir), "--target", "ps2_iop", "--parallel"])
    compile_database = iop_build_dir / "compile_commands.json"
    if not compile_database.is_file():
        raise RuntimeError("CMake did not emit compile_commands.json for the strict IOP target.")
    compilation_entries = json.loads(compile_database.read_text(encoding="utf-8"))
    interpreter_entries = [
        entry for entry in compilation_entries
        if Path(entry.get("file", "")).name == "iop_cpu.cpp"
    ]
    if interpreter_entries:
        raise RuntimeError(
            "Strict IOP target still compiles the guest interpreter: "
            + ", ".join(entry["file"] for entry in interpreter_entries)
        )
    print("Verified: compile_commands.json contains no iop_cpu.cpp interpreter source.")

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
        print("Strict IOP AOT-only static library was also compiled; no game ELF was accessed and no game-derived C++ was generated.")
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

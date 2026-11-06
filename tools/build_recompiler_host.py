#!/usr/bin/env python3
"""Build the KIexster-pinned PS2Recomp host tool and generate GoW C++ privately.

This script does not build the Android APK or the PS2 runtime. It only runs the
host-side static recompiler. Keep the ELF and generated C++ outside this repo.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
import shutil
import struct
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



@dataclass(frozen=True)
class Ps2ElfInfo:
    file_size: int
    entry_point: int
    program_headers: int
    loadable_segments: int


def validate_ps2_elf(path: Path) -> Ps2ElfInfo:
    """Validate the ELF32 little-endian MIPS executable shape expected by the PS2 runtime."""
    elf32_header_size = 52
    elf32_program_header_size = 32
    max_program_headers = 4096
    et_exec = 2
    em_mips = 8
    pt_load = 1
    address_space_end = 1 << 32

    file_size = path.stat().st_size
    if file_size < elf32_header_size:
        raise ValueError("ELF is shorter than an ELF32 header.")

    with path.open("rb") as stream:
        header = stream.read(elf32_header_size)
        if len(header) != elf32_header_size:
            raise ValueError("Could not read the complete ELF32 header.")

        if header[0:4] != b"\x7fELF":
            raise ValueError("Invalid ELF magic.")
        if header[4] != 1:
            raise ValueError("Unsupported ELF class; expected 32-bit ELF.")
        if header[5] != 1:
            raise ValueError("Unsupported ELF byte order; expected little-endian.")
        if header[6] != 1:
            raise ValueError("Unsupported ELF identification version.")

        (
            elf_type,
            machine,
            version,
            entry_point,
            program_header_offset,
            _section_header_offset,
            _flags,
            header_size,
            program_header_entry_size,
            program_header_count,
            _section_header_entry_size,
            _section_header_count,
            _section_name_index,
        ) = struct.unpack_from("<HHIIIIIHHHHHH", header, 16)

        if elf_type != et_exec:
            raise ValueError(f"ELF type {elf_type} is not ET_EXEC.")
        if machine != em_mips:
            raise ValueError(f"ELF machine {machine} is not EM_MIPS.")
        if version != 1:
            raise ValueError("Unsupported ELF header version.")
        if header_size < elf32_header_size or header_size > file_size:
            raise ValueError("ELF header size is invalid.")
        if program_header_count == 0 or program_header_count > max_program_headers:
            raise ValueError("ELF program-header count is empty or exceeds the safety limit.")
        if program_header_entry_size != elf32_program_header_size:
            raise ValueError("Unsupported ELF32 program-header entry size.")

        table_size = program_header_count * program_header_entry_size
        table_end = program_header_offset + table_size
        if program_header_offset < elf32_header_size or table_end > file_size:
            raise ValueError("ELF program-header table is outside the file.")

        loadable_segments = 0
        entry_is_loadable = False
        for index in range(program_header_count):
            offset = program_header_offset + index * program_header_entry_size
            stream.seek(offset)
            raw = stream.read(elf32_program_header_size)
            if len(raw) != elf32_program_header_size:
                raise ValueError(f"ELF program header {index} is truncated.")

            (
                segment_type,
                file_offset,
                virtual_address,
                _physical_address,
                file_bytes,
                memory_bytes,
                _segment_flags,
                _alignment,
            ) = struct.unpack("<IIIIIIII", raw)

            if segment_type != pt_load or memory_bytes == 0:
                continue
            if file_bytes > memory_bytes:
                raise ValueError(f"ELF PT_LOAD segment {index} has p_filesz greater than p_memsz.")
            if file_offset + file_bytes > file_size:
                raise ValueError(f"ELF PT_LOAD segment {index} exceeds the file bounds.")
            memory_end = virtual_address + memory_bytes
            if memory_end > address_space_end:
                raise ValueError(f"ELF PT_LOAD segment {index} overflows the 32-bit address space.")

            loadable_segments += 1
            if virtual_address <= entry_point < memory_end:
                entry_is_loadable = True

    if loadable_segments == 0:
        raise ValueError("ELF contains no non-empty PT_LOAD segment.")
    if not entry_is_loadable:
        raise ValueError("ELF entry point is not covered by a PT_LOAD segment.")

    return Ps2ElfInfo(
        file_size=file_size,
        entry_point=entry_point,
        program_headers=program_header_count,
        loadable_segments=loadable_segments,
    )


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
        help="build the pinned host tools/runtime on Linux without requiring or reading a game ELF",
    )
    parser.add_argument(
        "--android-runtime-only",
        action="store_true",
        help="cross-compile only ps2_runtime and ps2_iop for Android arm64-v8a with the pinned NDK",
    )
    parser.add_argument(
        "--prepare-android-runtime",
        action="store_true",
        help="prepare the pinned, patched Waffle source tree for the Gradle/CMake Android build",
    )
    parser.add_argument(
        "--export-runtime-dir",
        default=None,
        help="ignored android/app/build directory where the prepared Waffle source tree is exported",
    )
    parser.add_argument(
        "--export-game-cpp-dir",
        default=None,
        help="copy generated game .cpp/.h files to an ignored android/app/build directory for NDK compilation",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    elf: Path | None = None
    if not (args.build_only or args.android_runtime_only or args.prepare_android_runtime):
        if not args.elf:
            parser.error("Provide --elf or set GOW_ELF to your own extracted SCUS_973.99 file.")
        elf = Path(args.elf).expanduser().resolve()
        if not elf.is_file():
            parser.error(f"ELF file not found: {elf}")
        try:
            elf_info = validate_ps2_elf(elf)
        except (OSError, ValueError) as error:
            parser.error(f"Invalid PS2 ELF '{elf}': {error}")
        print(
            "Validated PS2 ELF32/MIPS: "
            f"entry=0x{elf_info.entry_point:08X}, "
            f"PT_LOAD={elf_info.loadable_segments}, "
            f"program_headers={elf_info.program_headers}, "
            f"bytes={elf_info.file_size}"
        )

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
    if not (args.build_only or args.android_runtime_only or args.prepare_android_runtime) and (not funcmap.is_file() or not template_path.is_file()):
        parser.error("The imported function map or recompilation template is missing.")

    sources = work / "sources"
    recompiler_source = sources / "PS2Recomp"
    kiexter_source = sources / "KIexster"
    build_dir = work / "build-recompiler"
    input_dir = work / "input"
    generated_dir = work / "generated"

    work.mkdir(parents=True, exist_ok=True)
    print(f"Private workspace: {work}")
    if args.prepare_android_runtime:
        print("Mode: prepare Android runtime source for Gradle/CMake; no game files are read or generated.")
    elif args.android_runtime_only:
        print("Mode: Android runtime cross-build only; no game files are read or generated.")
    elif args.build_only:
        print("Mode: host-tool/runtime build only; no game files are read or generated.")
    else:
        print("Target profile: God of War PS2 NTSC-U / SCUS_973.99")
    print("This stage uses private upstream worktrees and does not modify the Android APK.")

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

    # Reuse the repository's ARM64 NEON shim for the SSE-style VU1 native block API.
    neon_compat_source = repo_root / "port/kiexter/vu1/ps2_vu1_neon_compat.h"
    if not neon_compat_source.is_file():
        raise RuntimeError(f"Required ARM64 VU1 compatibility header is missing: {neon_compat_source}")
    neon_compat_target = recompiler_source / "ps2xRuntime/include/runtime/ps2_vu1_neon_compat.h"
    native_header = recompiler_source / "ps2xRuntime/include/runtime/ps2_vu1_native.h"
    if not native_header.is_file():
        raise RuntimeError(f"Waffle VU1 native header is missing: {native_header}")
    shutil.copy2(neon_compat_source, neon_compat_target)
    replace_exactly_once(
        native_header,
        "#include <immintrin.h>\n",
        "#if defined(__aarch64__) && defined(__ARM_NEON)\n"
        '#include "runtime/ps2_vu1_neon_compat.h"\n'
        "#else\n"
        "#include <immintrin.h>\n"
        "#endif\n",
        "select NEON compatibility layer on Android ARM64 and SSE on host x86-64",
    )
    threaded_gs = recompiler_source / "ps2xRuntime/src/lib/gs/gs_threaded_backend.cpp"
    replace_exactly_once(
        threaded_gs,
        "#include <xmmintrin.h>\n",
        "#if defined(__aarch64__) && defined(__ARM_NEON)\n"
        '#include "runtime/ps2_vu1_neon_compat.h"\n'
        "#else\n"
        "#include <xmmintrin.h>\n"
        "#endif\n",
        "select NEON FPCR/FPSR state support for the threaded GS worker",
    )

    runtime_header = recompiler_source / "ps2xRuntime/include/ps2_runtime.h"
    replace_exactly_once(
        runtime_header,
        "    bool initialize(const char *title = \"PS2 Game\");\n",
        "    bool initialize(const char *title = \"PS2 Game\");\n"
        "    // Android core-only setup: no Raylib window/audio or guest execution.\n"
        "    bool initializeCoreHeadless();\n",
        "expose a Raylib-free Android core initialization stage",
    )
    replace_exactly_once(
        runtime_header,
        "    bool m_hostWindowOwned = false;\n",
        "    bool m_hostWindowOwned = false;\n"
        "    bool m_coreInitialized = false;\n",
        "track core initialization independently from host graphics",
    )

    runtime_cpp = recompiler_source / "ps2xRuntime/src/lib/ps2_runtime.cpp"
    replace_exactly_once(
        runtime_cpp,
        "#include <xmmintrin.h>\n",
        "#if defined(__aarch64__) && defined(__ARM_NEON)\n"
        '#include "runtime/ps2_vu1_neon_compat.h"\n'
        "#else\n"
        "#include <xmmintrin.h>\n"
        "#endif\n",
        "select sse2neon plus ARM FPCR/FPSR shim for the EE/VU0 runtime",
    )
    replace_exactly_once(
        runtime_cpp,
        "#if !defined(PLATFORM_VITA)\n"
        "        stopSpu2Audio(); // GOW-Port: antes de destruir el IOP que produce las muestras\n"
        "#endif\n",
        "#if !defined(PLATFORM_VITA) && !defined(PS2X_ANDROID_LIFECYCLE_MANAGED)\n"
        "        stopSpu2Audio(); // GOW-Port: antes de destruir el IOP que produce las muestras\n"
        "#endif\n",
        "skip Raylib SPU2 stream teardown when Android never initialized desktop audio",
    )
    replace_exactly_once(
        runtime_cpp,
        "#if defined(PLATFORM_VITA)\\n"
        "        m_audioBackend.stopAll();\\n"
        "        m_audioBackend.setAudioReady(false);\\n"
        "#else\\n"
        "        if (IsAudioDeviceReady())\\n"
        "        {\\n"
        "            CloseAudioDevice();\\n"
        "            m_audioBackend.setAudioReady(false);\\n"
        "        }\\n"
        "#endif\\n"
        "        if (m_debugUiInitialized && m_debugUiShutdownCallback)",
        "#if defined(PLATFORM_VITA) || defined(PS2X_ANDROID_LIFECYCLE_MANAGED)\\n"
        "        m_audioBackend.stopAll();\\n"
        "        m_audioBackend.setAudioReady(false);\\n"
        "#else\\n"
        "        if (IsAudioDeviceReady())\\n"
        "        {\\n"
        "            CloseAudioDevice();\\n"
        "            m_audioBackend.setAudioReady(false);\\n"
        "        }\\n"
        "#endif\\n"
        "        if (m_debugUiInitialized && m_debugUiShutdownCallback)",
        "reset the core audio backend without calling Raylib during Android teardown",
    )

    # Android has no Raylib host window yet. Keep the PS2 core's default GS
    # backend inert until the GameActivity-owned Vulkan GS adapter is attached.
    gs_frontend = recompiler_source / "ps2xRuntime/src/lib/gs/gs_frontend.cpp"
    replace_exactly_once(
        gs_frontend,
        "GS::GS()\n"
        "    : m_backend(GSThreadedBackend::MakeDefault())\n"
        "{",
        "GS::GS()\n"
        "#if defined(PS2X_ANDROID_LIFECYCLE_MANAGED)\n"
        "    : m_backend(std::make_unique<GSNullBackend>())\n"
        "#else\n"
        "    : m_backend(GSThreadedBackend::MakeDefault())\n"
        "#endif\n"
        "{",
        "use a no-op GS raster backend during Android core-only initialization",
    )

    # Android reads DualShock 2 state from the app-owned GameActivity bridge;
    # desktop platforms continue using the upstream Raylib polling backend.
    pad_cpp = recompiler_source / "ps2xRuntime/src/lib/ps2_pad.cpp"
    replace_exactly_once(
        pad_cpp,
        '#include "ps2_host_backend.h"\n',
        '#include "ps2_host_backend.h"\n'
        '#if defined(__ANDROID__)\n'
        'extern "C" bool gowAndroidPadReadState(int port, int slot, uint8_t *data, size_t size);\n'
        '#endif\n',
        "declare Android GameActivity pad bridge",
    )
    replace_exactly_once(
        pad_cpp,
        "bool PSPadBackend::readState(int /*port*/, int /*slot*/, uint8_t *data, size_t size)\n{\n"
        "    if (!data || size < 32)\n",
        "bool PSPadBackend::readState(int port, int slot, uint8_t *data, size_t size)\n{\n"
        "#if defined(__ANDROID__)\n"
        "    return gowAndroidPadReadState(port, slot, data, size);\n"
        "#else\n"
        "    if (!data || size < 32)\n",
        "route Android PS2 pad reads through the atomic GameActivity input state",
    )
    replace_exactly_once(
        pad_cpp,
        "    return true;\n}\n\nvoid PSPadBackend::setActuators",
        "    return true;\n#endif\n}\n\nvoid PSPadBackend::setActuators",
        "close the Android-specific PS2 pad input branch",
    )
    replace_exactly_once(
        pad_cpp,
        "#else\n"
        "    // Other platforms (SDL, Switch): raylib's rumble, which stops after at most 2 s, so it is resent every second.\n",
        "#elif defined(__ANDROID__)\n"
        "    // Android haptics are separate from Raylib/GLFW; retain actuator state for the future Java bridge.\n"
        "    (void)refresh;\n"
        "#else\n"
        "    // Other platforms (SDL, Switch): raylib's rumble, which stops after at most 2 s, so it is resent every second.\n",
        "prevent desktop Raylib rumble calls in Android runtime",
    )


    # On Android, cdrom0: resolves through the private cdRoot VFS rather
    # than a copied host directory. The app bridge exposes random-access file handles.
    iop_host_header = recompiler_source / "ps2xRuntime/src/lib/ps2_iop_host.h"
    replace_exactly_once(
        iop_host_header,
        "        uint64_t size = 0u;\n",
        "        uint64_t size = 0u;\n"
        "        uint64_t androidCdRootHandle = 0u;\n",
        "add an Android cdRoot handle to the IOP host-file record",
    )

    iop_host_cpp = recompiler_source / "ps2xRuntime/src/lib/ps2_iop_host.cpp"
    replace_exactly_once(
        iop_host_cpp,
        '#include "ps2_iop_host.h"\n',
        '#include "ps2_iop_host.h"\n'
        '#if defined(__ANDROID__)\n'
        'extern "C" uint64_t gowAndroidCdRootOpenFile(const char *path, uint64_t *fileSize) noexcept;\n'
        'extern "C" bool gowAndroidCdRootReadFile(uint64_t handle, uint64_t offset, void *destination, size_t requested, size_t *bytesRead) noexcept;\n'
        'extern "C" void gowAndroidCdRootCloseFile(uint64_t handle) noexcept;\n'
        'extern "C" bool gowAndroidCdRootStat(const char *path, bool *isDirectory, uint64_t *fileSize) noexcept;\n'
        '#endif\n',
        "declare the Android private cdRoot handle bridge in the IOP adapter",
    )
    replace_exactly_once(
        iop_host_cpp,
        "        if (file.stream)\n"
        "        {\n"
        "            std::fclose(file.stream);\n"
        "        }",
        "        if (file.stream)\n"
        "        {\n"
        "            std::fclose(file.stream);\n"
        "        }\n"
        "#if defined(__ANDROID__)\n"
        "        if (file.androidCdRootHandle != 0u)\n"
        "            gowAndroidCdRootCloseFile(file.androidCdRootHandle);\n"
        "#endif",
        "close native cdRoot handles when the IOP host adapter is destroyed",
    )
    replace_exactly_once(
        iop_host_cpp,
        "uint64_t PS2IopHostAdapter::openHostFile(std::string_view path)\n{\n"
        "    if (path.empty())\n"
        "    {\n"
        "        return 0u;\n"
        "    }\n\n"
        "    const std::filesystem::path hostPath{std::string(path)};",
        "uint64_t PS2IopHostAdapter::openHostFile(std::string_view path)\n{\n"
        "    if (path.empty())\n"
        "    {\n"
        "        return 0u;\n"
        "    }\n"
        "#if defined(__ANDROID__)\n"
        "    constexpr std::string_view cdRootPrefix{\"gowcdroot:/\"};\n"
        "    if (path.size() >= cdRootPrefix.size() && path.substr(0u, cdRootPrefix.size()) == cdRootPrefix)\n"
        "    {\n"
        "        const std::string relativePath(path.substr(cdRootPrefix.size()));\n"
        "        uint64_t fileSize = 0u;\n"
        "        const uint64_t cdRootHandle = gowAndroidCdRootOpenFile(relativePath.c_str(), &fileSize);\n"
        "        if (cdRootHandle == 0u)\n"
        "            return 0u;\n"
        "        std::lock_guard<std::mutex> lock(m_hostFileMutex);\n"
        "        uint64_t handle = m_nextHostFileHandle++;\n"
        "        if (handle == 0u)\n"
        "            handle = m_nextHostFileHandle++;\n"
        "        m_hostFiles.emplace(handle, HostFile{nullptr, fileSize, cdRootHandle});\n"
        "        return handle;\n"
        "    }\n"
        "#endif\n\n"
        "    const std::filesystem::path hostPath{std::string(path)};",
        "open CD-root files through the native Android ISO reader",
    )
    replace_exactly_once(
        iop_host_cpp,
        "    if (it == m_hostFiles.end() || !it->second.stream)\n"
        "    {\n"
        "        return false;\n"
        "    }\n"
        "    size = it->second.size;",
        "    if (it == m_hostFiles.end() ||\n"
        "        (!it->second.stream && it->second.androidCdRootHandle == 0u))\n"
        "    {\n"
        "        return false;\n"
        "    }\n"
        "    size = it->second.size;",
        "report sizes for Android cdRoot-backed IOP handles",
    )
    replace_exactly_once(
        iop_host_cpp,
        "    if (it == m_hostFiles.end() || !it->second.stream)\n"
        "    {\n"
        "        return false;\n"
        "    }\n\n"
        "#if defined(_WIN32)",
        "    if (it == m_hostFiles.end())\n"
        "    {\n"
        "        return false;\n"
        "    }\n"
        "#if defined(__ANDROID__)\n"
        "    if (it->second.androidCdRootHandle != 0u)\n"
        "        return gowAndroidCdRootReadFile(it->second.androidCdRootHandle, offset, destination, size, &bytesRead);\n"
        "#endif\n"
        "    if (!it->second.stream)\n"
        "        return false;\n\n"
        "#if defined(_WIN32)",
        "read IOP file requests directly from private cdRoot files",
    )
    replace_exactly_once(
        iop_host_cpp,
        "    std::FILE *stream = nullptr;\n"
        "    {\n"
        "        std::lock_guard<std::mutex> lock(m_hostFileMutex);\n"
        "        const auto it = m_hostFiles.find(handle);\n"
        "        if (it == m_hostFiles.end())\n"
        "        {\n"
        "            return;\n"
        "        }\n"
        "        stream = it->second.stream;\n"
        "        m_hostFiles.erase(it);\n"
        "    }\n"
        "    if (stream)\n"
        "    {\n"
        "        std::fclose(stream);\n"
        "    }",
        "    std::FILE *stream = nullptr;\n"
        "    uint64_t cdRootHandle = 0u;\n"
        "    {\n"
        "        std::lock_guard<std::mutex> lock(m_hostFileMutex);\n"
        "        const auto it = m_hostFiles.find(handle);\n"
        "        if (it == m_hostFiles.end())\n"
        "        {\n"
        "            return;\n"
        "        }\n"
        "        stream = it->second.stream;\n"
        "        cdRootHandle = it->second.androidCdRootHandle;\n"
        "        m_hostFiles.erase(it);\n"
        "    }\n"
        "    if (stream)\n"
        "    {\n"
        "        std::fclose(stream);\n"
        "    }\n"
        "#if defined(__ANDROID__)\n"
        "    if (cdRootHandle != 0u)\n"
        "        gowAndroidCdRootCloseFile(cdRootHandle);\n"
        "#endif",
        "release IOP file handles through the matching host or Android disc backend",
    )

    # The PS2 VFS is also used by HLE stubs; give it the same read-only ISO provider.
    vfs_cpp = recompiler_source / "ps2xRuntime/src/lib/ps2_vfs.cpp"
    replace_exactly_once(
        vfs_cpp,
        '#include "runtime/ps2_rom_device.h"\n',
        '#include "runtime/ps2_rom_device.h"\n'
        '#if defined(__ANDROID__)\n'
        'extern "C" uint64_t gowAndroidCdRootOpenFile(const char *path, uint64_t *fileSize) noexcept;\n'
        'extern "C" bool gowAndroidCdRootReadFile(uint64_t handle, uint64_t offset, void *destination, size_t requested, size_t *bytesRead) noexcept;\n'
        'extern "C" void gowAndroidCdRootCloseFile(uint64_t handle) noexcept;\n'
        'extern "C" bool gowAndroidCdRootStat(const char *path, bool *isDirectory, uint64_t *fileSize) noexcept;\n'
        '#endif\n',
        "declare the Android cdRoot provider for the PS2 VFS",
    )
    replace_exactly_once(
        vfs_cpp,
        "namespace\n{\n    class HostOpenFile final : public IPS2OpenFile",
        "namespace\n{\n"
        "#if defined(__ANDROID__)\n"
        "    class AndroidCdRootOpenFile final : public IPS2OpenFile\n"
        "    {\n"
        "    public:\n"
        "        AndroidCdRootOpenFile(uint64_t handle, uint64_t size) : m_handle(handle), m_size(size) {}\n"
        "        ~AndroidCdRootOpenFile() override { if (m_handle != 0u) gowAndroidCdRootCloseFile(m_handle); }\n"
        "        int64_t read(void *destination, size_t size) override\n"
        "        {\n"
        "            if ((!destination && size != 0u) || m_handle == 0u) return -1;\n"
        "            size_t bytesRead = 0u;\n"
        "            if (!gowAndroidCdRootReadFile(m_handle, m_position, destination, size, &bytesRead)) return -1;\n"
        "            m_position += bytesRead;\n"
        "            return static_cast<int64_t>(bytesRead);\n"
        "        }\n"
        "        int64_t write(const void *, size_t) override { return -1; }\n"
        "        int64_t seek(int64_t offset, int whence) override\n"
        "        {\n"
        "            int64_t base = 0;\n"
        "            if (whence == SEEK_CUR) base = static_cast<int64_t>(m_position);\n"
        "            else if (whence == SEEK_END) base = static_cast<int64_t>(m_size);\n"
        "            else if (whence != SEEK_SET) return -1;\n"
        "            if ((offset > 0 && base > std::numeric_limits<int64_t>::max() - offset) ||\n"
        "                (offset < 0 && base < std::numeric_limits<int64_t>::min() - offset)) return -1;\n"
        "            const int64_t target = base + offset;\n"
        "            if (target < 0 || static_cast<uint64_t>(target) > m_size) return -1;\n"
        "            m_position = static_cast<uint64_t>(target);\n"
        "            return target;\n"
        "        }\n"
        "    private:\n"
        "        uint64_t m_handle = 0u;\n"
        "        uint64_t m_size = 0u;\n"
        "        uint64_t m_position = 0u;\n"
        "    };\n"
        "#endif\n\n"
        "    class HostOpenFile final : public IPS2OpenFile",
        "add the Android cdRoot-backed PS2 VFS file implementation",
    )
    vfs_open_old = (
        "        std::filesystem::path hostPath;\n"
        "        if (!resolveHostPath(path, mounts, hostPath))\n"
        "            return -1;\n\n"
        "        std::error_code existsError;\n"
        "        const bool exists = std::filesystem::exists(hostPath, existsError);\n"
        "        if (existsError || (exists && (flags & (PS2_FIO_O_CREAT | PS2_FIO_O_EXCL)) == (PS2_FIO_O_CREAT | PS2_FIO_O_EXCL)))\n"
        "        {\n"
        "            return -1;\n"
        "        }\n\n"
        "        FILE *stream = std::fopen(hostPath.string().c_str(), hostMode(flags));\n"
        "        const uint32_t access = flags & PS2_FIO_O_RDWR;\n"
        "        if (!stream && !exists && (flags & PS2_FIO_O_CREAT) != 0u &&\n"
        "            access == PS2_FIO_O_RDWR &&\n"
        "            (flags & (PS2_FIO_O_TRUNC | PS2_FIO_O_APPEND)) == 0u)\n"
        "        {\n"
        "            stream = std::fopen(hostPath.string().c_str(), \"w+b\");\n"
        "        }\n"
        "        if (!stream)\n"
        "            return -1;\n"
        "        file = std::make_unique<HostOpenFile>(stream);"
    )
    vfs_open_new = (
        "#if defined(__ANDROID__)\n"
        "        if (parsed.device == ps2x::iop::Ps2PathDevice::Cdrom)\n"
        "        {\n"
        "            const uint32_t access = flags & PS2_FIO_O_RDWR;\n"
        "            if (access != PS2_FIO_O_RDONLY || (flags & (PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC | PS2_FIO_O_APPEND)) != 0u) return -1;\n"
        "            uint64_t fileSize = 0u;\n"
        "            const uint64_t cdRootHandle = gowAndroidCdRootOpenFile(parsed.path.c_str(), &fileSize);\n"
        "            if (cdRootHandle == 0u) return -1;\n"
        "            file = std::make_unique<AndroidCdRootOpenFile>(cdRootHandle, fileSize);\n"
        "        }\n"
        "        else\n"
        "#endif\n"
        "        {\n"
        "            std::filesystem::path hostPath;\n"
        "            if (!resolveHostPath(path, mounts, hostPath))\n"
        "                return -1;\n\n"
        "            std::error_code existsError;\n"
        "            const bool exists = std::filesystem::exists(hostPath, existsError);\n"
        "            if (existsError || (exists && (flags & (PS2_FIO_O_CREAT | PS2_FIO_O_EXCL)) == (PS2_FIO_O_CREAT | PS2_FIO_O_EXCL)))\n"
        "            {\n"
        "                return -1;\n"
        "            }\n\n"
        "            FILE *stream = std::fopen(hostPath.string().c_str(), hostMode(flags));\n"
        "            const uint32_t access = flags & PS2_FIO_O_RDWR;\n"
        "            if (!stream && !exists && (flags & PS2_FIO_O_CREAT) != 0u &&\n"
        "                access == PS2_FIO_O_RDWR &&\n"
        "                (flags & (PS2_FIO_O_TRUNC | PS2_FIO_O_APPEND)) == 0u)\n"
        "            {\n"
        "                stream = std::fopen(hostPath.string().c_str(), \"w+b\");\n"
        "            }\n"
        "            if (!stream)\n"
        "                return -1;\n"
        "            file = std::make_unique<HostOpenFile>(stream);\n"
        "        }"
    )
    replace_exactly_once(
        vfs_cpp,
        vfs_open_old,
        vfs_open_new,
        "route Android cdrom0: VFS opens to the private cdRoot provider",
    )
    stat_anchor = (
        "    if (!resolveHostPath(path, mounts, hostPath))\n"
        "        return false;\n"
        "    std::error_code error;\n"
    )
    stat_replace = (
        "#if defined(__ANDROID__)\n"
        "    if (parsed.device == ps2x::iop::Ps2PathDevice::Cdrom)\n"
        "    {\n"
        "        bool isDirectory = false;\n"
        "        uint64_t fileSize = 0u;\n"
        "        if (!gowAndroidCdRootStat(parsed.path.c_str(), &isDirectory, &fileSize)) return false;\n"
        "        result.directory = isDirectory;\n"
        "        result.size = fileSize;\n"
        "        result.readOnly = true;\n"
        "        return true;\n"
        "    }\n"
        "#endif\n"
        "    if (!resolveHostPath(path, mounts, hostPath))\n"
        "        return false;\n"
        "    std::error_code error;\n"
    )
    replace_exactly_once(vfs_cpp, stat_anchor, stat_replace,
                         "support file stat for Android private cdRoot paths")

    replace_exactly_once(
        runtime_cpp,
        '#include "ps2_runtime.h"\n',
        '#include "ps2_runtime.h"\n'
        '#if defined(__ANDROID__)\n'
        'extern "C" bool gowAndroidCdRootCopyPath(char *destination, size_t capacity) noexcept;\n'
        '#endif\n',
        "declare Android cdRoot mount readiness for runtime path setup",
    )
    replace_exactly_once(
        runtime_cpp,
        "        paths.cdRoot = paths.elfDirectory;\n",
        "#if defined(__ANDROID__)\n"
        "        char androidCdRootPath[4096]{};\n"
        "        if (gowAndroidCdRootCopyPath(androidCdRootPath, sizeof(androidCdRootPath)))\n"
        "            paths.cdRoot = std::filesystem::u8path(androidCdRootPath);\n"
        "        else\n"
        "            paths.cdRoot = paths.elfDirectory;\n"
        "#else\n"
        "        paths.cdRoot = paths.elfDirectory;\n"
        "#endif\n",
        "mount the private cdRoot as the PS2 runtime CD root when available",
    )

    # The strict Android profile must not retain the optional PCSX2 VU1 bridge.
    replace_exactly_once(
        runtime_cpp,
        '#include "vu_pcsx2/pcsx2_vu1_bridge.h"\nstatic bool gowUsePcsx2Vu1(); // GOW_VU1_PCSX2=1, defined before PS2Runtime::initialize\n',
        '#if !defined(PS2X_VU1_NATIVE_ONLY)\n'
        '#include "vu_pcsx2/pcsx2_vu1_bridge.h"\n'
        'static bool gowUsePcsx2Vu1(); // GOW_VU1_PCSX2=1, defined before PS2Runtime::initialize\n'
        '#endif\n',
        "exclude the optional PCSX2 VU1 bridge from strict Android runtime",
    )
    runtime_text = runtime_cpp.read_text(encoding="utf-8")
    pcxs2_branch = "                                     if (gowUsePcsx2Vu1())\n"
    if runtime_text.count(pcxs2_branch) != 2:
        raise RuntimeError("Expected exactly two optional PCSX2 VU1 callback branches.")
    runtime_text = runtime_text.replace(
        pcxs2_branch,
        "#if !defined(PS2X_VU1_NATIVE_ONLY)\n" + pcxs2_branch,
    )
    for fallback in ("m_vu1.execute(", "m_vu1.resume("):
        old = "                                     else\n                                     " + fallback
        new = "                                     else\n#endif\n                                     " + fallback
        if runtime_text.count(old) != 1:
            raise RuntimeError(f"Expected one PCSX2 else branch before {fallback}.")
        runtime_text = runtime_text.replace(old, new, 1)
    runtime_cpp.write_text(runtime_text, encoding="utf-8")
    replace_exactly_once(
        runtime_cpp,
        "bool PS2Runtime::initialize(const char *title)\n{",
        "bool PS2Runtime::initializeCoreHeadless()\n"
        "{\n"
        "#if !defined(PS2X_ANDROID_LIFECYCLE_MANAGED)\n"
        "    std::cerr << \"Headless core initialization is only available in the Android-managed build profile.\" << std::endl;\n"
        "    return false;\n"
        "#else\n"
        "    if (m_coreInitialized)\n"
        "        return true;\n"
        "    try\n"
        "    {\n"
        "        if (!m_memory.initialize())\n"
        "        {\n"
        "            std::cerr << \"Failed to initialize PS2 memory for Android core.\" << std::endl;\n"
        "            return false;\n"
        "        }\n"
        "        if (!syncCoreSubsystems())\n"
        "        {\n"
        "            m_memory.stopGsWorker();\n"
        "            std::cerr << \"Failed to bind PS2 EE/IOP/VU core subsystems.\" << std::endl;\n"
        "            return false;\n"
        "        }\n"
        "        m_coreInitialized = true;\n"
        "        std::cerr << \"[PS2Runtime] Android headless core initialized; GS presentation, audio, ELF loading and guest execution remain separate stages.\" << std::endl;\n"
        "        return true;\n"
        "    }\n"
        "    catch (const std::exception &e)\n"
        "    {\n"
        "        std::cerr << \"Failed to initialize Android PS2 core: \" << e.what() << std::endl;\n"
        "    }\n"
        "    catch (...)\n"
        "    {\n"
        "        std::cerr << \"Failed to initialize Android PS2 core: unknown exception\" << std::endl;\n"
        "    }\n"
        "    m_memory.stopGsWorker();\n"
        "    return false;\n"
        "#endif\n"
        "}\n\n"
        "bool PS2Runtime::initialize(const char *title)\n{\n"
        "#if defined(PS2X_ANDROID_LIFECYCLE_MANAGED)\n"
        "    (void)title;\n"
        "    std::cerr << \"PS2Runtime::initialize() desktop host path is disabled on Android; use initializeCoreHeadless() until Android GS/audio adapters are attached.\" << std::endl;\n"
        "    return false;\n"
        "#endif\n",
        "separate Android core initialization from desktop Raylib host initialization",
    )
    replace_exactly_once(
        runtime_cpp,
        "        m_hostPresentation->restoreHostContext();\n",
        "        m_coreInitialized = true;\n"
        "        m_hostPresentation->restoreHostContext();\n",
        "mark the desktop core initialized after the host context binds",
    )
    replace_exactly_once(
        runtime_cpp,
        "bool PS2Runtime::loadELF(const std::string &elfPath)\n{\n",
        "bool PS2Runtime::loadELF(const std::string &elfPath)\n{\n"
        "    if (!m_coreInitialized)\n"
        "    {\n"
        "        std::cerr << \"Cannot load the PS2 ELF before core initialization.\" << std::endl;\n"
        "        return false;\n"
        "    }\n",
        "require initialized PS2 core before loading the guest ELF",
    )
    replace_exactly_once(
        runtime_cpp,
        "void PS2Runtime::run()\n{\n",
        "void PS2Runtime::run()\n{\n"
        "#if defined(PS2X_ANDROID_LIFECYCLE_MANAGED)\n"
        "    std::cerr << \"PS2Runtime::run() is blocked until Android GS/audio presentation and private AOT game code are ready.\" << std::endl;\n"
        "    return;\n"
        "#endif\n"
        "    if (!m_coreInitialized)\n"
        "    {\n"
        "        std::cerr << \"Cannot execute guest code before PS2 core initialization.\" << std::endl;\n"
        "        return;\n"
        "    }\n",
        "block Android guest execution until presentation and AOT prerequisites are met",
    )
    replace_exactly_once(
        runtime_cpp,
        "static bool gowUsePcsx2Vu1()\n{",
        "#if !defined(PS2X_VU1_NATIVE_ONLY)\nstatic bool gowUsePcsx2Vu1()\n{",
        "compile the PCSX2 VU1 environment switch only in non-strict builds",
    )
    replace_exactly_once(
        runtime_cpp,
        "    return enabled;\n}\n\nbool PS2Runtime::initializeCoreHeadless",
        "    return enabled;\n}\n#endif\n\nbool PS2Runtime::initializeCoreHeadless",
        "close the optional PCSX2 VU1 environment switch guard",
    )

    # Strict VU1 policy: never fall back to the cycle-exact interpreter.
    vu_core = recompiler_source / "ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp"
    replace_exactly_once(
        vu_core,
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
    replace_exactly_once(
        vu_core,
        """    if (m_unit == Unit::VU1 && !m_native)
        m_native = new NativeState();""",
        """    if (m_unit == Unit::VU1 && !m_native)
        m_native = new NativeState();
#if defined(PS2X_VU1_NATIVE_ONLY)
    if (m_unit == Unit::VU1 && m_native)
        m_native->enabled = true;
#endif""",
        "force VU1 native dispatch enabled in strict builds",
    )
    replace_exactly_once(
        vu_core,
        """    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)
        VU1NativeAccess::recordReplay(*this, &context, maxCycles);""",
        """#if !defined(PS2X_VU1_NATIVE_ONLY)
    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)
        VU1NativeAccess::recordReplay(*this, &context, maxCycles);
#endif""",
        "exclude VU1 interpreter record-replay from strict runtime",
    )
    replace_exactly_once(
        vu_core,
        """    if (m_unit == Unit::VU1 && m_native && m_native->enabled && m_native->verifyLeft != 0u && vuData && codeSize == PS2_VU1_CODE_SIZE)
    {""",
        """#if !defined(PS2X_VU1_NATIVE_ONLY)
    if (m_unit == Unit::VU1 && m_native && m_native->enabled && m_native->verifyLeft != 0u && vuData && codeSize == PS2_VU1_CODE_SIZE)
    {""",
        "exclude VU1 interpreter A/B verifier from strict runtime",
    )
    replace_exactly_once(
        vu_core,
        """        else if (checked % 2000u == 0u)
            std::fprintf(stderr, "[vu1n-verify] %u runs checked, %u mismatches\\n", checked, bad);
    }
    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)
    {
        if (!runNativeLoop(context))
            runExact(context);
    }
    else
        runExact(context);""",
        """        else if (checked % 2000u == 0u)
            std::fprintf(stderr, "[vu1n-verify] %u runs checked, %u mismatches\\n", checked, bad);
    }
#endif
#if defined(PS2X_VU1_NATIVE_ONLY)
    if (m_unit == Unit::VU1)
    {
        if (vuData != nullptr && codeSize == PS2_VU1_CODE_SIZE)
            runNativeLoop(context);
        else
        {
            std::fprintf(stderr, "[VU1:native-required] invalid VU1 execution buffer: codeSize=%u data=%p\\n", codeSize, static_cast<void *>(vuData));
            m_stopRequested = true;
        }
    }
    else
        runExact(context);
#else
    if (m_unit == Unit::VU1 && m_native && m_native->enabled && vuData && codeSize == PS2_VU1_CODE_SIZE)
    {
        if (!runNativeLoop(context))
            runExact(context);
    }
    else
        runExact(context);
#endif""",
        "strict VU1 runtime dispatch without interpreter fallback",
    )

    # Empty shell-only symbols keep the Android shell linkable before the private
    # game ELF has been recompiled. Disable this source when generated game code is linked.
    placeholder_source = recompiler_source / "ps2xRuntime/src/lib/android_recompiled_table_placeholder.cpp"
    placeholder_source.write_text(
        '#include "ps2_runtime.h"\n\n'
        'extern const uint32_t g_ps2RecompiledFunctionTableBase = 0u;\n'
        'extern const uint32_t g_ps2RecompiledFunctionTableEnd = 0u;\n'
        'extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = 0u;\n'
        'PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1] = {};\n',
        encoding="utf-8",
    )

    vu_cmake = recompiler_source / "ps2xRuntime/CMakeLists.txt"
    replace_exactly_once(
        vu_cmake,
        "add_library(ps2_runtime STATIC\n",
        "# FORGE owns Android lifecycle through AndroidX GameActivity. Raylib 5.5\n"
        "# also defines android_main() for its NativeActivity runner. Rename that\n"
        "# unused entry point when embedding Raylib so it cannot collide with the\n"
        "# app's GameActivity glue. Other Raylib APIs remain temporarily available\n"
        "# until the upstream host adapters are replaced.\n"
        "if(PS2X_IS_ANDROID AND TARGET raylib)\n"
        "    target_compile_definitions(raylib PRIVATE android_main=ps2x_raylib_android_main)\n"
        "endif()\n\n"
        "add_library(ps2_runtime STATIC\n",
        "isolate Raylib NativeActivity entry point from FORGE GameActivity",
    )
    diagnostic_sources = [
        "src/lib/ee_ref/ee_diff.cpp",
        "src/lib/ee_watch.cpp",
        "src/lib/ee_ref/eeref.cpp",
        "src/lib/ee_ref/R5900OpcodeImpl.cpp",
        "src/lib/ee_ref/R5900OpcodeTables.cpp",
        "src/lib/ee_ref/MMI.cpp",
        "src/lib/ee_ref/FPU.cpp",
        "src/lib/vu_pcsx2/pcsx2_vu0_cop2.cpp",
        "src/lib/vu_pcsx2/pcsx2_vu1_bridge.cpp",
        "src/lib/vu_pcsx2/VUops.cpp",
        "src/lib/vu_pcsx2/VUflags.cpp",
        "src/lib/vu_pcsx2/VU1microInterp.cpp",
    ]
    for source in diagnostic_sources:
        replace_exactly_once(
            vu_cmake,
            f"    {source}\n",
            "",
            f"remove diagnostic-only source from strict VU1 target: {source}",
        )
    replace_exactly_once(
        vu_cmake,
        "    src/lib/games_database.cpp\n)\n",
        "    src/lib/games_database.cpp\n)\n"
        "if(NOT PS2X_VU1_NATIVE_ONLY)\n"
        "    target_sources(ps2_runtime PRIVATE\n"
        + "".join(f"        {source}\n" for source in diagnostic_sources)
        + "    )\n"
        "endif()\n",
        "keep PCSX2 differential files only in non-strict diagnostic builds",
    )
    replace_exactly_once(
        vu_cmake,
        'option(PS2X_ENABLE_IOP_RPC_TRACE "Log unhandled IOP/SIF RPC trace suggestions" ON)\n',
        'option(PS2X_ENABLE_IOP_RPC_TRACE "Log unhandled IOP/SIF RPC trace suggestions" ON)\n'
        'option(PS2X_VU1_NATIVE_ONLY "Reject VU1 exact-engine fallback when AOT coverage is missing" OFF)\n'
        'if(ANDROID)\n'
        '    set(PS2X_VU1_NATIVE_ONLY ON CACHE BOOL "" FORCE)\n'
        'endif()\n',
        "VU1 native-only CMake option",
    )
    replace_exactly_once(
        vu_cmake,
        "target_compile_definitions(ps2_runtime PRIVATE\n    PS2X_HAS_FFMPEG=$<BOOL:${PS2X_ENABLE_FFMPEG}>\n)\n",
        "if(PS2X_VU1_NATIVE_ONLY)\n"
        "    target_compile_definitions(ps2_runtime PRIVATE PS2X_VU1_NATIVE_ONLY=1)\n"
        "endif()\n"
        "if(ANDROID)\n"
        "    target_compile_definitions(ps2_runtime PRIVATE PS2X_ANDROID_LIFECYCLE_MANAGED=1)\n"
        "endif()\n\n"
        "target_compile_definitions(ps2_runtime PRIVATE\n    PS2X_HAS_FFMPEG=$<BOOL:${PS2X_ENABLE_FFMPEG}>\n)\n",
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

    if args.prepare_android_runtime:
        default_export = repo_root / "android/app/build/gow-runtime-source"
        export_dir = (
            Path(args.export_runtime_dir).expanduser().resolve()
            if args.export_runtime_dir
            else default_export
        )
        build_root = (repo_root / "android/app/build").resolve()
        try:
            export_dir.relative_to(build_root)
        except ValueError:
            parser.error("--export-runtime-dir must be inside android/app/build (ignored by Git).")
        if export_dir == build_root:
            parser.error("--export-runtime-dir must name a dedicated subdirectory of android/app/build.")
        if export_dir.exists():
            shutil.rmtree(export_dir)
        export_dir.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(
            recompiler_source,
            export_dir,
            ignore=shutil.ignore_patterns(".git", ".gitmodules"),
        )
        if not (export_dir / "CMakeLists.txt").is_file():
            raise RuntimeError("Exported Waffle source tree is missing its root CMakeLists.txt.")
        print(f"Prepared Waffle source exported to ignored build directory: {export_dir}")
        print(f"Source commits: PS2Recomp={PS2RECOMP_COMMIT}; KIexster={KIEXTER_COMMIT}")
        print("Verified: no ELF, ISO, IRX, microcode, or generated game C++ was copied.")
        return 0

    if args.android_runtime_only:
        sdk_root = (
            os.environ.get("ANDROID_SDK_ROOT")
            or os.environ.get("ANDROID_HOME")
        )
        ndk_version = os.environ.get("GOW_ANDROID_NDK_VERSION", "30.0.16248370")
        sdk_pinned_ndk = Path(sdk_root) / "ndk" / ndk_version if sdk_root else None
        if sdk_pinned_ndk and (sdk_pinned_ndk / "build/cmake/android.toolchain.cmake").is_file():
            ndk_root = str(sdk_pinned_ndk)
        else:
            ndk_root = os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT")
        if ndk_root:
            print(f"Using Android NDK: {ndk_root}")
        if not ndk_root:
            raise RuntimeError(
                "Android SDK/NDK not found. Set ANDROID_SDK_ROOT or ANDROID_NDK_HOME."
            )
        toolchain = Path(ndk_root) / "build/cmake/android.toolchain.cmake"
        if not toolchain.is_file():
            raise RuntimeError(f"Android NDK CMake toolchain not found: {toolchain}")

        android_build_dir = work / "build-android-arm64-strict"
        android_configure = [
            cmake,
            "-S", str(recompiler_source),
            "-B", str(android_build_dir),
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            "-DANDROID_ABI=arm64-v8a",
            "-DANDROID_PLATFORM=android-28",
            "-DANDROID_STL=c++_shared",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DPS2X_BUILD_RECOMP=OFF",
            "-DPS2X_BUILD_RUNTIME=ON",
            "-DPS2X_BUILD_ANALYZER=OFF",
            "-DPS2X_BUILD_TEST=OFF",
            "-DPS2X_BUILD_STUDIO=OFF",
            "-DPS2X_IOP_NATIVE_ONLY=ON",
            "-DPS2X_VU1_NATIVE_ONLY=ON",
            "-DPS2X_ENABLE_FFMPEG=OFF",
            "-DPS2X_ENABLE_DEBUG_UI=OFF",
            "-DPS2X_ENABLE_RUNNER_UNITY_BUILD=OFF",
            "-DPS2X_ENABLE_RUNNER_PCH=OFF",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ]
        if generator:
            android_configure.extend(["-G", "Ninja"])
        run(android_configure)
        # Build only the static runtime library. The upstream NativeActivity/Raylib
        # runner is intentionally not built or linked into FORGE/GameActivity.
        run([
            cmake, "--build", str(android_build_dir),
            "--target", "ps2_runtime", "--parallel",
        ])

        android_compile_db = android_build_dir / "compile_commands.json"
        if not android_compile_db.is_file():
            raise RuntimeError("CMake did not emit the Android runtime compile database.")
        android_entries = json.loads(android_compile_db.read_text(encoding="utf-8"))
        def normalized_source(entry: dict[str, object]) -> str:
            return str(entry.get("file", "")).replace("\\\\", "/")

        vu_android_entries = [
            entry for entry in android_entries
            if Path(normalized_source(entry)).name == "ps2_vu1_core.cpp"
        ]
        if (
            len(vu_android_entries) != 1
            or "PS2X_VU1_NATIVE_ONLY=1" not in str(vu_android_entries[0].get("command", ""))
        ):
            raise RuntimeError(
                "Android VU1 core was not compiled exactly once with PS2X_VU1_NATIVE_ONLY=1."
            )
        iop_android_entries = [
            entry for entry in android_entries
            if Path(normalized_source(entry)).name == "iop_cpu.cpp"
        ]
        if iop_android_entries:
            raise RuntimeError("Android strict runtime unexpectedly compiled iop_cpu.cpp.")
        diagnostic_android_entries = [
            entry for entry in android_entries
            if "/ee_ref/" in normalized_source(entry)
            or "/vu_pcsx2/" in normalized_source(entry)
            or Path(normalized_source(entry)).name == "ee_watch.cpp"
        ]
        if diagnostic_android_entries:
            raise RuntimeError(
                "Android strict runtime unexpectedly compiled PCSX2 diagnostic sources: "
                + ", ".join(normalized_source(entry) for entry in diagnostic_android_entries)
            )
        print("Verified: Android arm64-v8a runtime compiled with strict IOP/VU1 policies.")
        print("Verified: Android target excludes iop_cpu.cpp and PCSX2 diagnostic interpreters.")
        print("Verified: only static ps2_runtime and dependencies were built; NativeActivity runner was not built.")
        return 0

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

    # Compile the Waffle runtime's VU1 core with strict AOT dispatch. No game microcode
    # is supplied; this validates compilation of the no-interpreter fallback path.
    vu_build_dir = work / "build-vu1-native-only"
    vu_configure = [
        cmake,
        "-S", str(recompiler_source),
        "-B", str(vu_build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DPS2X_BUILD_RECOMP=OFF",
        "-DPS2X_BUILD_RUNTIME=ON",
        "-DPS2X_BUILD_ANALYZER=OFF",
        "-DPS2X_BUILD_TEST=OFF",
        "-DPS2X_BUILD_STUDIO=OFF",
        "-DPS2X_IOP_NATIVE_ONLY=ON",
        "-DPS2X_VU1_NATIVE_ONLY=ON",
        "-DPS2X_ENABLE_FFMPEG=OFF",
        "-DPS2X_ENABLE_DEBUG_UI=OFF",
        "-DPS2X_ENABLE_RUNNER_UNITY_BUILD=OFF",
        "-DPS2X_ENABLE_RUNNER_PCH=OFF",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    if generator:
        vu_configure.extend(["-G", "Ninja"])
    run(vu_configure)
    run([cmake, "--build", str(vu_build_dir), "--target", "ps2_runtime", "--parallel"])
    vu_compile_db = vu_build_dir / "compile_commands.json"
    if not vu_compile_db.is_file():
        raise RuntimeError("CMake did not emit the strict VU1 compile database.")
    vu_entries = json.loads(vu_compile_db.read_text(encoding="utf-8"))
    vu_core_entries = [
        entry for entry in vu_entries
        if Path(entry.get("file", "")).name == "ps2_vu1_core.cpp"
    ]
    if len(vu_core_entries) != 1 or "PS2X_VU1_NATIVE_ONLY=1" not in vu_core_entries[0].get("command", ""):
        raise RuntimeError("VU1 runtime source was not compiled exactly once with strict native-only mode.")
    print("Verified: ps2_vu1_core.cpp compiled with PS2X_VU1_NATIVE_ONLY=1.")
    diagnostic_entries = [
        entry for entry in vu_entries
        if "/ee_ref/" in entry.get("file", "").replace("\\\\", "/")
        or "/vu_pcsx2/" in entry.get("file", "").replace("\\\\", "/")
        or Path(entry.get("file", "")).name == "ee_watch.cpp"
    ]
    if diagnostic_entries:
        raise RuntimeError(
            "Strict VU1 runtime unexpectedly compiled PCSX2 diagnostic sources: "
            + ", ".join(entry["file"] for entry in diagnostic_entries)
        )
    print("Verified: PCSX2 differential interpreter sources are excluded from the strict runtime target.")

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

    # The template uses individual-function output, so generated .cpp files can
    # be nested below generated_dir rather than stored only in its root.
    generated_files = sorted(generated_dir.rglob("*.cpp"))
    function_sources = [path for path in generated_files if path.name != "register_functions.cpp"]
    required_outputs = (
        generated_dir / "register_functions.cpp",
        generated_dir / "ps2_recompiled_functions.h",
        generated_dir / "ps2_recompiled_stubs.h",
    )
    missing_outputs = [path.name for path in required_outputs if not path.is_file()]
    if not function_sources or missing_outputs:
        raise RuntimeError(
            "PS2Recomp output is incomplete. "
            f"Function C++ files: {len(function_sources)}; missing: {', '.join(missing_outputs) or 'none'}."
        )

    print("")
    print(f"Generated game C++ files: {len(function_sources)}")
    print(f"Generated registration file: {generated_dir / 'register_functions.cpp'}")
    print(f"Private output: {generated_dir}")

    if args.export_game_cpp_dir:
        export_dir = Path(args.export_game_cpp_dir).expanduser().resolve()
        build_root = (repo_root / "android/app/build").resolve()
        try:
            export_dir.relative_to(build_root)
        except ValueError:
            parser.error("--export-game-cpp-dir must be inside android/app/build (ignored by Git).")
        if export_dir == build_root:
            parser.error("--export-game-cpp-dir must name a dedicated subdirectory of android/app/build.")
        if export_dir.exists():
            shutil.rmtree(export_dir)
        export_dir.mkdir(parents=True, exist_ok=True)

        exported_cpp = 0
        exported_headers = 0
        for path in generated_dir.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in {".cpp", ".h", ".hpp", ".inc", ".ipp"}:
                continue
            destination = export_dir / path.relative_to(generated_dir)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
            if path.suffix.lower() == ".cpp":
                exported_cpp += 1
            else:
                exported_headers += 1

        for required in ("register_functions.cpp", "ps2_recompiled_functions.h", "ps2_recompiled_stubs.h"):
            if not (export_dir / required).is_file():
                raise RuntimeError(f"Exported game-code staging is missing {required}.")
        if exported_cpp != len(generated_files):
            raise RuntimeError(
                f"Export mismatch: expected {len(generated_files)} C++ files, copied {exported_cpp}."
            )
        print(f"Android-private staging: {export_dir}")
        print(f"Exported generated files: {exported_cpp} C++ and {exported_headers} headers/includes")
        print("The Android CMake target will compile these sources and omit the empty shell table.")

    print("Not done by this script: VU1/IOP AOT, Android runtime execution, boot, or gameplay testing.")
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

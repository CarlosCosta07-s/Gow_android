#!/usr/bin/env python3
"""Inventory extracted PS2 IOP IRX modules without attempting unsafe AOT generation.

The PS2Recomp IOP loader may relocate ET_SCE_IOPRELEXEC modules at runtime and
applies MIPS relocation records before execution. This tool only identifies
ELF images that appear fixed-address under the pinned loader's placement rules.
It does not emit native code and does not prove the runtime module key.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
from pathlib import Path
from typing import Any

ELF32_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
ELF32_PROGRAM_HEADER = struct.Struct("<IIIIIIII")
ELF32_SECTION_HEADER = struct.Struct("<IIIIIIIIII")
ELF32_REL = struct.Struct("<II")
ELF32_RELA = struct.Struct("<IIi")

ET_EXEC = 2
ET_SCE_IOPRELEXEC = 0xFF80
ET_SCE_IOPRELEXEC2 = 0xFF81
EM_MIPS = 8
PT_LOAD = 1
PT_SCE_IOPMOD = 0x70000080
PT_MIPS_REGINFO = 0x70000000
SHT_REL = 9
SHT_RELA = 4
SHF_ALLOC = 0x2

R_MIPS_SUPPORTED = {0, 1, 2, 3, 4, 5, 6}
IOP_RAM_SIZE = 2 * 1024 * 1024
MODULE_LOAD_BASE = 0x10000
MAX_IRX_BYTES = 64 * 1024 * 1024
MAX_IRX_FILES = 4096
MAX_SECTION_HEADERS = 4096
MAX_PROGRAM_HEADERS = 4096


class IrxFormatError(ValueError):
    """An IRX is not a safe, bounded ELF32 little-endian MIPS image."""


def _checked_table(data_size: int, offset: int, entry_size: int, count: int, label: str) -> None:
    if (
        count < 0
        or (label == "program" and count > MAX_PROGRAM_HEADERS)
        or (label == "section" and count > MAX_SECTION_HEADERS)
    ):
        raise IrxFormatError(f"{label} header count exceeds the safety limit")
    if count == 0:
        return
    if entry_size <= 0 or offset > data_size or entry_size * count > data_size - offset:
        raise IrxFormatError(f"{label} header table is outside the IRX file")


def inspect_irx(path: Path, relative_path: str) -> dict[str, Any]:
    """Inspect one IRX and classify its placement/relocation requirements."""
    if path.is_symlink() or not path.is_file():
        raise IrxFormatError("IRX path is not a regular file")
    size_bytes = path.stat().st_size
    if size_bytes < ELF32_HEADER.size or size_bytes > MAX_IRX_BYTES:
        raise IrxFormatError("IRX size is outside the supported bounds")
    data = path.read_bytes()

    header = ELF32_HEADER.unpack_from(data, 0)
    ident = header[0]
    (
        elf_type, machine, version, entry, phoff, shoff, _flags, ehsize,
        phentsize, phnum, shentsize, shnum, _shstrndx,
    ) = header[1:]

    if ident[:4] != b"\x7fELF" or ident[4] != 1 or ident[5] != 1 or ident[6] != 1:
        raise IrxFormatError("IRX is not ELF32 little-endian version 1")
    if machine != EM_MIPS:
        raise IrxFormatError(f"IRX machine is not MIPS ({machine})")
    if version != 1 or ehsize < ELF32_HEADER.size:
        raise IrxFormatError("IRX ELF header version/size is invalid")
    _checked_table(len(data), phoff, phentsize, phnum, "program")
    _checked_table(len(data), shoff, shentsize, shnum, "section")
    if phnum and phentsize < ELF32_PROGRAM_HEADER.size:
        raise IrxFormatError("IRX program-header entry is too small")
    if shnum and shentsize < ELF32_SECTION_HEADER.size:
        raise IrxFormatError("IRX section-header entry is too small")

    program_headers: list[tuple[int, ...]] = []
    load_segments: list[tuple[int, int, int, int, int]] = []
    module_entry = entry
    module_gp: int | None = None
    fallback_gp: int | None = None

    for index in range(phnum):
        offset = phoff + index * phentsize
        program = ELF32_PROGRAM_HEADER.unpack_from(data, offset)
        program_headers.append(program)
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, p_flags, _p_align = program

        if p_type == PT_LOAD and p_memsz:
            if p_filesz > p_memsz or p_offset > len(data) or p_filesz > len(data) - p_offset:
                raise IrxFormatError(f"PT_LOAD segment {index} has an invalid file/memory range")
            end = p_vaddr + p_memsz
            if end > 0x100000000:
                raise IrxFormatError(f"PT_LOAD segment {index} overflows the IOP address space")
            load_segments.append((p_vaddr, end, p_offset, p_filesz, p_flags))

        if p_type == PT_SCE_IOPMOD and p_filesz >= 12:
            if p_offset > len(data) or 12 > len(data) - p_offset:
                raise IrxFormatError("PT_SCE_IOPMOD metadata is outside the IRX file")
            _module_info, module_entry, module_gp = struct.unpack_from("<III", data, p_offset)

        if p_type == PT_MIPS_REGINFO and p_filesz >= 24:
            if p_offset > len(data) or 24 > len(data) - p_offset:
                raise IrxFormatError("PT_MIPS_REGINFO metadata is outside the IRX file")
            fallback_gp = struct.unpack_from("<I", data, p_offset + 20)[0]

    if module_gp in (None, 0):
        module_gp = fallback_gp

    sections: list[tuple[int, ...]] = []
    relocation_sections = 0
    relocation_count = 0
    relocation_types: set[int] = set()
    unsupported_relocations: set[int] = set()

    for index in range(shnum):
        offset = shoff + index * shentsize
        section = ELF32_SECTION_HEADER.unpack_from(data, offset)
        sections.append(section)
        (
            _name, section_type, section_flags, _address, section_offset,
            section_size, link, info, _alignment, entry_size,
        ) = section

        if section_type in (SHT_REL, SHT_RELA):
            relocation_sections += 1
            expected = ELF32_RELA.size if section_type == SHT_RELA else ELF32_REL.size
            actual_entry_size = max(entry_size, expected)
            if actual_entry_size <= 0 or section_size % actual_entry_size != 0:
                raise IrxFormatError(f"relocation section {index} has an invalid entry size")
            if section_offset > len(data) or section_size > len(data) - section_offset:
                raise IrxFormatError(f"relocation section {index} is outside the IRX file")
            if info >= shnum or link >= shnum:
                raise IrxFormatError(f"relocation section {index} has invalid section references")
            for position in range(section_offset, section_offset + section_size, actual_entry_size):
                relocation_info = struct.unpack_from("<I", data, position + 4)[0]
                relocation_type = relocation_info & 0xFF
                relocation_count += 1
                relocation_types.add(relocation_type)
                if relocation_type not in R_MIPS_SUPPORTED:
                    unsupported_relocations.add(relocation_type)

    has_load_segments = bool(load_segments)
    if has_load_segments:
        minimum_address = min(segment[0] for segment in load_segments)
        maximum_address = max(segment[1] for segment in load_segments)
        ordered = sorted(load_segments, key=lambda item: item[0])
        overlapping_segments = any(
            ordered[index][1] > ordered[index + 1][0]
            for index in range(len(ordered) - 1)
        )
    else:
        allocated = [
            section for section in sections
            if (section[2] & SHF_ALLOC) and section[5]
        ]
        minimum_address = min((section[3] for section in allocated), default=0)
        maximum_address = max((section[3] + section[5] for section in allocated), default=0)
        overlapping_segments = False

    span = max(0, maximum_address - minimum_address)
    is_iop_relocatable = elf_type in (ET_SCE_IOPRELEXEC, ET_SCE_IOPRELEXEC2)
    loader_would_relocate = (
        elf_type != ET_EXEC
        or maximum_address > IOP_RAM_SIZE
        or (minimum_address < MODULE_LOAD_BASE and minimum_address != 0)
    )

    reasons: list[str] = []
    if not has_load_segments:
        reasons.append("no PT_LOAD segments; section allocation may be dynamic")
    if elf_type != ET_EXEC:
        reasons.append(f"ELF type 0x{elf_type:04x} is not fixed ET_EXEC")
    if loader_would_relocate:
        reasons.append("pinned loader would relocate this image")
    if relocation_count:
        reasons.append(f"{relocation_count} MIPS relocation records require loader processing")
    if unsupported_relocations:
        reasons.append(
            "relocation types unsupported by the pinned loader: "
            + ",".join(f"0x{value:02x}" for value in sorted(unsupported_relocations))
        )
    if minimum_address < MODULE_LOAD_BASE:
        reasons.append("fixed image begins below the reserved module load base")
    if maximum_address > IOP_RAM_SIZE:
        reasons.append("image extends beyond 2 MiB IOP RAM")
    if overlapping_segments:
        reasons.append("PT_LOAD memory ranges overlap")
    if not (minimum_address <= module_entry < maximum_address):
        reasons.append("module entry point is outside the loaded image")

    fixed_image_candidate = (
        has_load_segments
        and elf_type == ET_EXEC
        and not loader_would_relocate
        and relocation_count == 0
        and not unsupported_relocations
        and not overlapping_segments
        and minimum_address >= MODULE_LOAD_BASE
        and maximum_address <= IOP_RAM_SIZE
        and minimum_address <= module_entry < maximum_address
    )
    if fixed_image_candidate:
        reasons.append(
            "fixed-address image candidate only; runtime module key, loaded bytes and memory conflicts still require validation"
        )

    return {
        "path": relative_path,
        "file_size": size_bytes,
        "elf_type": elf_type,
        "elf_type_name": (
            "ET_EXEC" if elf_type == ET_EXEC
            else "ET_SCE_IOPRELEXEC" if elf_type == ET_SCE_IOPRELEXEC
            else "ET_SCE_IOPRELEXEC2" if elf_type == ET_SCE_IOPRELEXEC2
            else f"0x{elf_type:04x}"
        ),
        "machine": "MIPS",
        "module_entry": module_entry,
        "module_gp": module_gp,
        "load_base": minimum_address,
        "loaded_span": span,
        "load_segments": len(load_segments),
        "relocation_sections": relocation_sections,
        "relocation_records": relocation_count,
        "relocation_types": sorted(relocation_types),
        "unsupported_relocation_types": sorted(unsupported_relocations),
        "loader_would_relocate": loader_would_relocate,
        "fixed_image_candidate": fixed_image_candidate,
        "reasons": reasons,
    }


def inventory_iop_irx(disc_root: Path) -> dict[str, Any]:
    """Find and classify all extracted .IRX files without following symlinks."""
    root_input = disc_root.expanduser()
    if root_input.is_symlink():
        raise ValueError("The extracted disc root may not be a symbolic link.")
    root = root_input.resolve()
    if not root.is_dir():
        raise ValueError(f"Extracted disc directory does not exist: {root}")

    candidates: list[Path] = []
    for current, directories, filenames in os.walk(root, topdown=True, followlinks=False):
        current_path = Path(current)
        directories[:] = [
            name for name in directories
            if not (current_path / name).is_symlink()
        ]
        for filename in filenames:
            path = current_path / filename
            if path.suffix.casefold() == ".irx" and not path.is_symlink():
                candidates.append(path)
                if len(candidates) > MAX_IRX_FILES:
                    raise ValueError("Extracted disc contains too many IRX files.")

    modules: list[dict[str, Any]] = []
    for path in sorted(candidates):
        relative = path.relative_to(root).as_posix()
        try:
            modules.append(inspect_irx(path, relative))
        except (OSError, IrxFormatError) as error:
            modules.append({
                "path": relative,
                "file_size": path.stat().st_size if path.exists() else None,
                "valid_elf": False,
                "fixed_image_candidate": False,
                "error": str(error),
            })

    valid = [module for module in modules if module.get("machine") == "MIPS"]
    return {
        "format": "gow-iop-irx-inventory-v1",
        "irx_files": len(modules),
        "valid_mips_irx": len(valid),
        "fixed_image_candidates": sum(
            1 for module in valid if module.get("fixed_image_candidate") is True
        ),
        "relocation_dependent": sum(
            1 for module in valid if module.get("loader_would_relocate") is True
        ),
        "modules": modules,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Classify extracted PS2 IOP IRX files; does not generate native AOT."
    )
    parser.add_argument("--disc-root", required=True, help="Private ISO extraction directory.")
    parser.add_argument("--json-out", help="Optional private JSON report path.")
    args = parser.parse_args()

    report = inventory_iop_irx(Path(args.disc_root))
    summary = {
        key: report[key]
        for key in (
            "irx_files", "valid_mips_irx", "fixed_image_candidates", "relocation_dependent"
        )
    }
    print("Private ISO IOP IRX inventory: " + json.dumps(summary, sort_keys=True))
    for module in report["modules"]:
        if module.get("fixed_image_candidate"):
            print(
                f"Fixed-address candidate: {module['path']} "
                f"base=0x{module['load_base']:x} span=0x{module['loaded_span']:x}; "
                "not yet approved for AOT"
            )
    if args.json_out:
        output = Path(args.json_out).expanduser()
        if output.exists() and output.is_symlink():
            raise ValueError("The private IRX inventory output may not be a symbolic link.")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Synthetic-only tests for the PS2 ELF preflight validator."""
from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_recompiler_host import validate_ps2_elf  # noqa: E402


def write_synthetic_elf(path: Path, **overrides: int) -> None:
    values = {
        "elf_class": 1,
        "endianness": 1,
        "ident_version": 1,
        "elf_type": 2,
        "machine": 8,
        "version": 1,
        "entry_point": 0x00100000,
        "program_header_offset": 52,
        "header_size": 52,
        "program_header_entry_size": 32,
        "program_header_count": 1,
        "segment_type": 1,
        "file_offset": 84,
        "virtual_address": 0x00100000,
        "file_bytes": 4,
        "memory_bytes": 8,
    }
    values.update(overrides)

    data = bytearray(88)
    data[0:4] = b"\x7fELF"
    data[4] = values["elf_class"]
    data[5] = values["endianness"]
    data[6] = values["ident_version"]
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        data,
        16,
        values["elf_type"],
        values["machine"],
        values["version"],
        values["entry_point"],
        values["program_header_offset"],
        0,  # section header offset
        0,  # flags
        values["header_size"],
        values["program_header_entry_size"],
        values["program_header_count"],
        0,
        0,
        0,
    )
    if values["program_header_offset"] + 32 <= len(data):
        struct.pack_into(
            "<IIIIIIII",
            data,
            values["program_header_offset"],
            values["segment_type"],
            values["file_offset"],
            values["virtual_address"],
            values["virtual_address"],
            values["file_bytes"],
            values["memory_bytes"],
            5,
            16,
        )
    path.write_bytes(data)


class Ps2ElfValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix="gow-elf-validator-")
        self.root = Path(self.temp_dir.name)
        self.elf = self.root / "synthetic-mips.elf"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_accepts_valid_elf32_mips_executable(self) -> None:
        write_synthetic_elf(self.elf)
        info = validate_ps2_elf(self.elf)
        self.assertEqual(info.file_size, 88)
        self.assertEqual(info.entry_point, 0x00100000)
        self.assertEqual(info.program_headers, 1)
        self.assertEqual(info.loadable_segments, 1)

    def test_rejects_invalid_ident_and_architecture(self) -> None:
        cases = (
            ({"elf_class": 2}, "32-bit"),
            ({"endianness": 2}, "little-endian"),
            ({"ident_version": 0}, "identification version"),
            ({"elf_type": 1}, "ET_EXEC"),
            ({"machine": 3}, "EM_MIPS"),
            ({"version": 0}, "header version"),
        )
        for overrides, message in cases:
            with self.subTest(overrides=overrides):
                write_synthetic_elf(self.elf, **overrides)
                with self.assertRaisesRegex(ValueError, message):
                    validate_ps2_elf(self.elf)

    def test_rejects_bad_program_header_table(self) -> None:
        cases = (
            ({"program_header_count": 0}, "count"),
            ({"program_header_count": 4097}, "safety limit"),
            ({"program_header_entry_size": 31}, "entry size"),
            ({"program_header_offset": 1000}, "outside the file"),
        )
        for overrides, message in cases:
            with self.subTest(overrides=overrides):
                write_synthetic_elf(self.elf, **overrides)
                with self.assertRaisesRegex(ValueError, message):
                    validate_ps2_elf(self.elf)

    def test_rejects_invalid_loadable_segment(self) -> None:
        cases = (
            ({"segment_type": 0}, "no non-empty PT_LOAD"),
            ({"file_bytes": 9, "memory_bytes": 8}, "p_filesz greater than p_memsz"),
            ({"file_offset": 86, "file_bytes": 4}, "file bounds"),
            ({"virtual_address": 0xFFFFFFF0, "memory_bytes": 0x40}, "address space"),
            ({"virtual_address": 0x00200000}, "entry point"),
        )
        for overrides, message in cases:
            with self.subTest(overrides=overrides):
                write_synthetic_elf(self.elf, **overrides)
                with self.assertRaisesRegex(ValueError, message):
                    validate_ps2_elf(self.elf)

    def test_rejects_short_file(self) -> None:
        self.elf.write_bytes(b"\x7fELF")
        with self.assertRaisesRegex(ValueError, "shorter than"):
            validate_ps2_elf(self.elf)

    def test_rejects_invalid_magic(self) -> None:
        write_synthetic_elf(self.elf)
        self.elf.write_bytes(b"NOPE" + self.elf.read_bytes()[4:])
        with self.assertRaisesRegex(ValueError, "magic"):
            validate_ps2_elf(self.elf)


if __name__ == "__main__":
    unittest.main()

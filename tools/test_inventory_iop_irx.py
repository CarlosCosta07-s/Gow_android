from __future__ import annotations

import shutil
import struct
import tempfile
import unittest
from pathlib import Path

from inventory_iop_irx import (
    ET_EXEC,
    ET_SCE_IOPRELEXEC,
    EM_MIPS,
    inventory_iop_irx,
    inspect_irx,
)


def make_elf(
    *,
    elf_type: int = ET_EXEC,
    entry: int = 0x10000,
    vaddr: int = 0x10000,
    relocations: bool = False,
) -> bytes:
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    phoff = 52
    phentsize = 32
    phnum = 1
    shoff = 0x180 if relocations else 0
    shentsize = 40
    shnum = 2 if relocations else 0
    header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        ident,
        elf_type,
        EM_MIPS,
        1,
        entry,
        phoff,
        shoff,
        0,
        52,
        phentsize,
        phnum,
        shentsize,
        shnum,
        0,
    )
    program = struct.pack(
        "<IIIIIIII",
        1,          # PT_LOAD
        0x100,      # p_offset
        vaddr,
        vaddr,
        16,         # p_filesz
        24,         # p_memsz
        5,          # PF_R | PF_X
        0x10,
    )
    image = bytearray(max(0x180, 0x100 + 16))
    image[: len(header)] = header
    image[phoff : phoff + len(program)] = program
    image[0x100 : 0x110] = bytes(range(16))
    if relocations:
        null_section = bytes(40)
        rel_section = struct.pack(
            "<IIIIIIIIII",
            0,
            9,          # SHT_REL
            0,
            0,
            0x1F0,
            8,
            0,          # symbol table
            1,          # target section
            4,
            8,
        )
        image[shoff : shoff + 40] = null_section
        image[shoff + 40 : shoff + 80] = rel_section
        image[0x1F0 : 0x1F8] = struct.pack("<II", 0, 2)  # R_MIPS_32
        if len(image) < 0x1F8:
            image.extend(bytes(0x1F8 - len(image)))
    return bytes(image)


class IopIrxInventoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gow-iop-irx-inventory-")
        self.root = Path(self.temporary.name) / "disc"
        (self.root / "IRX").mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_fixed_et_exec_without_relocations_is_candidate_only(self) -> None:
        path = self.root / "IRX/STATIC.IRX"
        path.write_bytes(make_elf())
        result = inspect_irx(path, "IRX/STATIC.IRX")
        self.assertTrue(result["fixed_image_candidate"])
        self.assertFalse(result["loader_would_relocate"])
        self.assertEqual(result["load_base"], 0x10000)
        self.assertEqual(result["loaded_span"], 24)
        self.assertIn("not yet approved for AOT", result["reasons"][-1])

    def test_iop_relocatable_elf_is_not_a_fixed_candidate(self) -> None:
        path = self.root / "IRX/RELOC.IRX"
        path.write_bytes(
            make_elf(
                elf_type=ET_SCE_IOPRELEXEC,
                entry=0x1000,
                vaddr=0x1000,
            )
        )
        result = inspect_irx(path, "IRX/RELOC.IRX")
        self.assertFalse(result["fixed_image_candidate"])
        self.assertTrue(result["loader_would_relocate"])
        self.assertEqual(result["elf_type"], ET_SCE_IOPRELEXEC)

    def test_et_exec_with_relocation_records_is_not_a_fixed_candidate(self) -> None:
        path = self.root / "IRX/RELOC_SECTION.IRX"
        path.write_bytes(make_elf(relocations=True))
        result = inspect_irx(path, "IRX/RELOC_SECTION.IRX")
        self.assertFalse(result["fixed_image_candidate"])
        self.assertEqual(result["relocation_sections"], 1)
        self.assertEqual(result["relocation_records"], 1)
        self.assertEqual(result["relocation_types"], [2])

    def test_inventory_finds_uppercase_irx_and_skips_non_irx_files(self) -> None:
        (self.root / "IRX/STATIC.IRX").write_bytes(make_elf())
        (self.root / "IRX/readme.txt").write_text("not an IRX", encoding="ascii")
        report = inventory_iop_irx(self.root)
        self.assertEqual(report["irx_files"], 1)
        self.assertEqual(report["valid_mips_irx"], 1)
        self.assertEqual(report["fixed_image_candidates"], 1)

    def test_invalid_irx_is_reported_without_aborting_inventory(self) -> None:
        (self.root / "IRX/BAD.IRX").write_bytes(b"not-elf")
        (self.root / "IRX/GOOD.IRX").write_bytes(make_elf())
        report = inventory_iop_irx(self.root)
        self.assertEqual(report["irx_files"], 2)
        bad = next(item for item in report["modules"] if item["path"].endswith("BAD.IRX"))
        good = next(item for item in report["modules"] if item["path"].endswith("GOOD.IRX"))
        self.assertFalse(bad.get("valid_elf", True))
        self.assertTrue(good["fixed_image_candidate"])

    def test_symlinked_disc_root_is_rejected(self) -> None:
        link = Path(self.temporary.name) / "disc-link"
        link.symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "may not be a symbolic link"):
            inventory_iop_irx(link)

    def test_symlinked_irx_is_not_followed(self) -> None:
        outside = Path(self.temporary.name) / "outside.IRX"
        outside.write_bytes(make_elf())
        (self.root / "IRX/LINK.IRX").symlink_to(outside)
        report = inventory_iop_irx(self.root)
        self.assertEqual(report["irx_files"], 0)

    def test_bad_program_table_is_rejected(self) -> None:
        image = bytearray(make_elf())
        struct.pack_into("<I", image, 28, 0xFFFFFFF0)  # e_phoff
        path = self.root / "IRX/BAD_TABLE.IRX"
        path.write_bytes(image)
        with self.assertRaisesRegex(ValueError, "program header table"):
            inspect_irx(path, "IRX/BAD_TABLE.IRX")


if __name__ == "__main__":
    unittest.main()

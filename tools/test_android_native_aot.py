from __future__ import annotations

import tempfile
import unittest
import zipfile
from pathlib import Path

from build_android_native_aot import (
    MAX_ARCHIVE_BYTES,
    VU1_MICRO_MEMORY_BYTES,
    fnv1a64,
    normalize_and_validate_iop_dump,
    safe_extract_archive,
    validate_vu1_capture_directory,
)


class AndroidNativeAotCaptureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gow-native-aot-")
        self.root = Path(self.temporary.name)
        self.capture = self.root / "captures"
        (self.capture / "vu1").mkdir(parents=True)
        (self.capture / "iop").mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_vu1_requires_exact_16_kib_micro_memory(self) -> None:
        path = self.capture / "vu1/vu1_001.bin"
        path.write_bytes(bytes(VU1_MICRO_MEMORY_BYTES))
        self.assertEqual(validate_vu1_capture_directory(self.capture / "vu1"), [path])

        path.write_bytes(bytes(VU1_MICRO_MEMORY_BYTES - 8))
        with self.assertRaisesRegex(ValueError, "expected exactly 16384"):
            validate_vu1_capture_directory(self.capture / "vu1")

    def test_vu1_empty_capture_is_valid_for_incremental_bootstrap(self) -> None:
        self.assertEqual(validate_vu1_capture_directory(self.capture / "vu1"), [])

    def test_iop_empty_capture_is_valid_for_incremental_bootstrap(self) -> None:
        records = normalize_and_validate_iop_dump(
            self.capture / "iop", self.root / "normalized"
        )
        self.assertEqual(records, [])
        self.assertTrue((self.root / "normalized").is_dir())

    def test_iop_manifest_is_verified_and_normalized(self) -> None:
        module = b"\x00\x00\x00\x00\x01\x00\x00\x00"
        (self.capture / "iop/mod_a.bin").write_bytes(module)
        digest = fnv1a64(module)
        (self.capture / "iop/modules.txt").write_text(
            f"mod_a 1f000 8 {digest:016x}\nmod_a 1f000 8 {digest:016x}\n",
            encoding="ascii",
        )
        output = self.root / "normalized"
        records = normalize_and_validate_iop_dump(self.capture / "iop", output)

        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["name"], "mod_a")
        self.assertEqual((output / "mod_a.bin").read_bytes(), module)
        self.assertEqual(
            (output / "modules.txt").read_text(encoding="ascii"),
            f"mod_a 1f000 8 {digest:x}\n",
        )

    def test_iop_hash_mismatch_is_rejected(self) -> None:
        module = b"\x00\x00\x00\x00"
        (self.capture / "iop/mod_a.bin").write_bytes(module)
        (self.capture / "iop/modules.txt").write_text(
            "mod_a 1f000 4 0000000000000000\n", encoding="ascii"
        )
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            normalize_and_validate_iop_dump(
                self.capture / "iop", self.root / "normalized"
            )

    def test_iop_same_name_at_multiple_bases_is_rejected(self) -> None:
        module = b"\x00\x00\x00\x00"
        (self.capture / "iop/mod_a.bin").write_bytes(module)
        digest = fnv1a64(module)
        (self.capture / "iop/modules.txt").write_text(
            f"mod_a 1f000 4 {digest:016x}\nmod_a 1f100 4 {digest:016x}\n",
            encoding="ascii",
        )
        with self.assertRaisesRegex(ValueError, "multiple load bases"):
            normalize_and_validate_iop_dump(
                self.capture / "iop", self.root / "normalized"
            )

    def test_zip_capture_archive_extracts_expected_private_layout(self) -> None:
        module = b"\x00\x00\x00\x00"
        (self.capture / "vu1/vu1_001.bin").write_bytes(bytes(VU1_MICRO_MEMORY_BYTES))
        (self.capture / "vu1/vu1n_heads.txt").write_text("0010\\n0020\\n", encoding="ascii")
        (self.capture / "iop/mod_a.bin").write_bytes(module)
        digest = fnv1a64(module)
        (self.capture / "iop/modules.txt").write_text(
            f"mod_a 1f000 4 {digest:016x}\n", encoding="ascii"
        )
        archive_path = self.root / "captures.zip"
        with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(self.capture.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(self.capture).as_posix())
            archive.writestr(
                "capture_manifest.txt",
                "format=gow-native-aot-captures-v1\n",
            )

        vu1_dir, iop_dir = safe_extract_archive(
            archive_path, self.root / "extracted"
        )
        self.assertEqual(validate_vu1_capture_directory(vu1_dir)[0].name, "vu1_001.bin")
        self.assertEqual(
            (vu1_dir / "vu1n_heads.txt").read_text(encoding="ascii"),
            "0010\\n0020\\n",
        )
        self.assertTrue((iop_dir / "modules.txt").is_file())
        self.assertEqual((iop_dir / "mod_a.bin").read_bytes(), module)

    def test_zip_path_traversal_is_rejected(self) -> None:
        archive_path = self.root / "unsafe.zip"
        with zipfile.ZipFile(archive_path, "w") as archive:
            archive.writestr("../outside.bin", b"not allowed")
        with self.assertRaisesRegex(ValueError, "Unsafe ZIP entry path"):
            safe_extract_archive(archive_path, self.root / "extracted")

    def test_archive_limit_is_bounded(self) -> None:
        self.assertEqual(MAX_ARCHIVE_BYTES, 512 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()

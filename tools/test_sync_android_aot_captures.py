from __future__ import annotations

import tempfile
import tarfile
import unittest
import zipfile
from pathlib import Path

from sync_android_aot_captures import (
    NoDeviceCaptures,
    capture_tar_to_zip,
)


class AndroidAotAdbCaptureSyncTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gow-adb-aot-")
        self.root = Path(self.temporary.name)
        self.tar_path = self.root / "captures.tar"
        self.zip_path = self.root / "captures.zip"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _create_tar(self, entries: list[tuple[str, bytes]]) -> None:
        with tarfile.open(self.tar_path, "w") as archive:
            for name, data in entries:
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o600
                import io
                archive.addfile(info, io.BytesIO(data))

    def test_private_capture_tree_is_converted_into_expected_zip(self) -> None:
        vu1 = bytes(16 * 1024)
        iop = b"synthetic-iop-module"
        self._create_tar([
            ("gow-aot-captures", b""),
            ("gow-aot-captures/vu1", b""),
            ("gow-aot-captures/iop", b""),
            ("gow-aot-captures/vu1/vu1_0001.bin", vu1),
            ("gow-aot-captures/vu1/vu1n_heads.txt", b"0010\n"),
            ("gow-aot-captures/iop/module_a.bin", iop),
            ("gow-aot-captures/iop/modules.txt", b"module_a 1f000 14 1234\n"),
        ])
        result = capture_tar_to_zip(self.tar_path, self.zip_path)
        self.assertEqual(result, self.zip_path)
        with zipfile.ZipFile(result) as archive:
            self.assertEqual(
                set(archive.namelist()),
                {
                    "vu1/vu1_0001.bin",
                    "vu1/vu1n_heads.txt",
                    "iop/module_a.bin",
                    "iop/modules.txt",
                    "capture_manifest.txt",
                },
            )
            self.assertEqual(archive.read("vu1/vu1_0001.bin"), vu1)
            self.assertIn(
                "source=android-private-files-via-adb",
                archive.read("capture_manifest.txt").decode("utf-8"),
            )

    def test_empty_private_capture_tree_requests_bootstrap(self) -> None:
        self._create_tar([
            ("gow-aot-captures", b""),
            ("gow-aot-captures/vu1", b""),
            ("gow-aot-captures/iop", b""),
            ("gow-aot-captures/vu1/vu1n_heads.txt", b""),
        ])
        with self.assertRaisesRegex(NoDeviceCaptures, "has not created"):
            capture_tar_to_zip(self.tar_path, self.zip_path)
        self.assertFalse(self.zip_path.exists())

    def test_capture_zip_rejects_path_traversal(self) -> None:
        self._create_tar([
            ("gow-aot-captures/vu1/../../outside.bin", b"private"),
        ])
        with self.assertRaisesRegex(ValueError, "Unsafe ADB capture path"):
            capture_tar_to_zip(self.tar_path, self.zip_path)

    def test_capture_zip_rejects_unexpected_files(self) -> None:
        self._create_tar([
            ("gow-aot-captures/vu1/SCUS_973.99", b"ELF"),
        ])
        with self.assertRaisesRegex(ValueError, "Unexpected file"):
            capture_tar_to_zip(self.tar_path, self.zip_path)

    def test_capture_zip_rejects_symbolic_links(self) -> None:
        with tarfile.open(self.tar_path, "w") as archive:
            link = tarfile.TarInfo("gow-aot-captures/vu1/link.bin")
            link.type = tarfile.SYMTYPE
            link.linkname = "../../private.bin"
            archive.addfile(link)
        with self.assertRaisesRegex(ValueError, "Non-regular AOT capture entry"):
            capture_tar_to_zip(self.tar_path, self.zip_path)

    def test_duplicate_paths_are_rejected(self) -> None:
        self._create_tar([
            ("gow-aot-captures/vu1/a.bin", b"first"),
            ("gow-aot-captures/vu1/a.bin", b"second"),
        ])
        with self.assertRaisesRegex(ValueError, "Duplicate private AOT capture"):
            capture_tar_to_zip(self.tar_path, self.zip_path)


if __name__ == "__main__":
    unittest.main()

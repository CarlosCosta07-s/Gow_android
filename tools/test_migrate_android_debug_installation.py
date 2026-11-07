from __future__ import annotations

import hashlib
import io
import tarfile
import tempfile
import unittest
from pathlib import Path

from migrate_android_debug_installation import (
    DEFAULT_NTSC_INSTALLATION_DIRECTORY,
    _parse_boot2,
    validate_private_installation_backup,
)


class PrivateAndroidInstallBackupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gow-debug-migration-")
        self.root = Path(self.temporary.name)
        self.archive = self.root / "private-disc.tar"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_backup(
        self,
        *,
        tamper_digest: bool = False,
        boot_target: bytes = b"BOOT2 = cdrom0:\\SCUS_973.99;1\n",
    ) -> None:
        prefix = (
            "installations/"
            + DEFAULT_NTSC_INSTALLATION_DIRECTORY
            + "/disc"
        )
        files = {
            "SYSTEM.CNF": boot_target,
            "SCUS_973.99": b"\x7fELF\x01\x01synthetic-boot-image",
            ".disc_layout": b"sectors 8\nlayer1 0\n",
            ".disc_meta": b"synthetic-sector-metadata",
        }
        manifest = bytearray(b"GOW-DISC-INTEGRITY\t1\n")
        for relative, payload in files.items():
            digest = hashlib.sha256(payload).hexdigest()
            if tamper_digest and relative == "SCUS_973.99":
                digest = "0" * 64
            manifest.extend(
                f"{digest}\t{len(payload)}\t{relative}\n".encode("utf-8")
            )

        with tarfile.open(self.archive, "w") as archive:
            for directory in (
                "installations",
                "installations/" + DEFAULT_NTSC_INSTALLATION_DIRECTORY,
                prefix,
            ):
                info = tarfile.TarInfo(directory)
                info.type = tarfile.DIRTYPE
                info.mode = 0o700
                archive.addfile(info)
            for relative, payload in files.items():
                info = tarfile.TarInfo(prefix + "/" + relative)
                info.size = len(payload)
                info.mode = 0o600
                archive.addfile(info, io.BytesIO(payload))
            info = tarfile.TarInfo(prefix + "/.gow_disc_integrity")
            info.size = len(manifest)
            info.mode = 0o600
            archive.addfile(info, io.BytesIO(bytes(manifest)))

    def test_complete_backup_and_sha256_manifest_are_accepted(self) -> None:
        self._write_backup()
        result = validate_private_installation_backup(self.archive)
        self.assertEqual(result["boot_relative_path"], "SCUS_973.99")
        self.assertEqual(result["installation_directory"], DEFAULT_NTSC_INSTALLATION_DIRECTORY)
        self.assertEqual(result["file_count"], 5)
        self.assertEqual(result["verified_disc_bytes"], sum(
            len(data) for data in (
                b"BOOT2 = cdrom0:\\SCUS_973.99;1\n",
                b"\x7fELF\x01\x01synthetic-boot-image",
                b"sectors 8\nlayer1 0\n",
                b"synthetic-sector-metadata",
            )
        ))
        self.assertEqual(len(result["backup_sha256"]), 64)

    def test_backup_rejects_modified_game_file_before_uninstall(self) -> None:
        self._write_backup(tamper_digest=True)
        with self.assertRaisesRegex(ValueError, "SHA-256 verification failed"):
            validate_private_installation_backup(self.archive)

    def test_backup_rejects_pal_or_wrong_boot_profile(self) -> None:
        self._write_backup(
            boot_target=b"BOOT2 = cdrom0:\\SCES_531.33;1\n"
        )
        with self.assertRaisesRegex(ValueError, "not the supported NTSC-U"):
            validate_private_installation_backup(self.archive)

    def test_boot2_parser_supports_a_nested_executable_path(self) -> None:
        self.assertEqual(
            _parse_boot2(br"BOOT2 = cdrom0:\BOOT\SCUS_973.99;1"),
            "BOOT/SCUS_973.99",
        )

    def test_boot2_parser_rejects_non_cdrom_devices(self) -> None:
        with self.assertRaisesRegex(ValueError, "does not point to cdrom0"):
            _parse_boot2(b"BOOT2 = host0:\\\\SCUS_973.99;1\\n")

    def test_backup_rejects_path_traversal(self) -> None:
        with tarfile.open(self.archive, "w") as archive:
            info = tarfile.TarInfo("../../outside.bin")
            payload = b"not allowed"
            info.size = len(payload)
            archive.addfile(info, io.BytesIO(payload))
        with self.assertRaisesRegex(ValueError, "Unsafe private-install backup path"):
            validate_private_installation_backup(self.archive)

    def test_backup_rejects_symlink_entries(self) -> None:
        with tarfile.open(self.archive, "w") as archive:
            link = tarfile.TarInfo(
                "installations/"
                + DEFAULT_NTSC_INSTALLATION_DIRECTORY
                + "/disc/link"
            )
            link.type = tarfile.SYMTYPE
            link.linkname = "../../outside"
            archive.addfile(link)
        with self.assertRaisesRegex(ValueError, "Non-regular private backup entry"):
            validate_private_installation_backup(self.archive)


if __name__ == "__main__":
    unittest.main()

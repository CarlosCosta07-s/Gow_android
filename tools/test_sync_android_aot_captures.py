from __future__ import annotations

import io
import os
import tempfile
import tarfile
import unittest
import zipfile
from unittest.mock import patch
from pathlib import Path

from sync_android_aot_captures import (
    NoDeviceCaptures,
    capture_tar_to_zip,
    find_adb_executable,
    pull_private_installed_file,
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
                info.mode = 0o700 if name in (
                    "gow-aot-captures",
                    "gow-aot-captures/vu1",
                    "gow-aot-captures/iop",
                ) else 0o600
                if name in (
                    "gow-aot-captures",
                    "gow-aot-captures/vu1",
                    "gow-aot-captures/iop",
                ):
                    info.type = tarfile.DIRTYPE
                    info.size = 0
                    archive.addfile(info)
                else:
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

    def test_private_installed_boot_file_is_pulled_without_manual_export(self) -> None:
        payload = b"BOOT2 = cdrom0:\\\\SCUS_973.99;1\\n"

        class FakeProcess:
            def __init__(self) -> None:
                self.stdout = io.BytesIO(payload)
                self.stderr = io.BytesIO(b"")
                self.killed = False

            def wait(self) -> int:
                return 0

            def poll(self) -> int:
                return 0

            def kill(self) -> None:
                self.killed = True

        process = FakeProcess()
        with patch("sync_android_aot_captures._select_device", return_value="device-1"):
            with patch("sync_android_aot_captures.subprocess.Popen", return_value=process) as popen:
                result = pull_private_installed_file(
                    self.root / "private-work",
                    self.root / "repo",
                    "SYSTEM.CNF",
                    adb_executable="/sdk/platform-tools/adb",
                    device_serial="device-1",
                    max_bytes=1024,
                )

        self.assertEqual(result.read_bytes(), payload)
        command = popen.call_args.args[0]
        self.assertEqual(command[:4], [
            "/sdk/platform-tools/adb", "-s", "device-1", "exec-out"
        ])
        self.assertIn(
            "run-as org.gowandroid.app.debug cat "
            "files/installations/gow_scus_973_99_ntsc_u/disc/SYSTEM.CNF",
            command[-1],
        )
        self.assertFalse(process.killed)

    def test_private_installed_file_rejects_traversal(self) -> None:
        with patch("sync_android_aot_captures.subprocess.Popen") as popen:
            with self.assertRaisesRegex(ValueError, "unsafe component"):
                pull_private_installed_file(
                    self.root / "private-work",
                    self.root / "repo",
                    "../SCUS_973.99",
                    adb_executable="/sdk/platform-tools/adb",
                    device_serial="device-1",
                )
        popen.assert_not_called()

    def test_private_installed_file_enforces_transfer_limit(self) -> None:
        payload = b"x" * 32

        class FakeProcess:
            def __init__(self) -> None:
                self.stdout = io.BytesIO(payload)
                self.stderr = io.BytesIO(b"")
                self.killed = False

            def wait(self) -> int:
                return -9 if self.killed else 0

            def poll(self) -> int:
                return -9 if self.killed else None

            def kill(self) -> None:
                self.killed = True

        process = FakeProcess()
        with patch("sync_android_aot_captures._select_device", return_value="device-1"):
            with patch("sync_android_aot_captures.subprocess.Popen", return_value=process):
                with self.assertRaisesRegex(ValueError, "transfer limit"):
                    pull_private_installed_file(
                        self.root / "private-work",
                        self.root / "repo",
                        "SCUS_973.99",
                        adb_executable="/sdk/platform-tools/adb",
                        device_serial="device-1",
                        max_bytes=16,
                    )
        self.assertTrue(process.killed)
        self.assertFalse(
            (self.root / "private-work/android-private-installation/SCUS_973.99").exists()
        )

    def test_adb_is_found_under_android_sdk_without_path_setup(self) -> None:
        sdk_platform_tools = self.root / "sdk" / "platform-tools"
        sdk_platform_tools.mkdir(parents=True)
        adb = sdk_platform_tools / "adb"
        adb.write_text("#!/bin/sh\\n", encoding="ascii")
        with patch("sync_android_aot_captures.shutil.which", return_value=None):
            with patch.dict(os.environ, {"ANDROID_SDK_ROOT": str(self.root / "sdk")}):
                self.assertEqual(find_adb_executable(), str(adb))


if __name__ == "__main__":
    unittest.main()

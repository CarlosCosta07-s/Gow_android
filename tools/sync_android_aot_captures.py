#!/usr/bin/env python3
"""Synchronize private VU1/IOP captures from a debuggable Android app via ADB.

No document picker or user-facing capture export is used. The ZIP is generated
inside the private host workspace and is consumed by build_android_native_aot.py.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_TAR_BYTES = MAX_ARCHIVE_BYTES + 16 * 1024 * 1024
MAX_FILES = 8192
DEFAULT_PACKAGE = "org.gowandroid.app.debug"


class NoDeviceCaptures(RuntimeError):
    """ADB/device/app is unavailable or no private game captures exist yet."""


def _safe_relative_capture(name: str) -> str | None:
    if not name or "\\" in name:
        raise ValueError(f"Unsafe ADB capture path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"Unsafe ADB capture path: {name!r}")
    parts = path.parts
    if len(parts) == 1 and parts[0] == "gow-aot-captures":
        return None
    if len(parts) == 2 and parts[0] == "gow-aot-captures" and parts[1] in ("vu1", "iop"):
        return None
    if len(parts) != 3 or parts[0] != "gow-aot-captures":
        raise ValueError(f"Unexpected file in private AOT capture tree: {name}")
    folder, filename = parts[1], parts[2]
    if folder == "vu1" and (filename.endswith(".bin") or filename == "vu1n_heads.txt"):
        return f"vu1/{filename}"
    if folder == "iop" and (filename.endswith(".bin") or filename == "modules.txt"):
        return f"iop/{filename}"
    raise ValueError(f"Unexpected file in private AOT capture tree: {name}")


def capture_tar_to_zip(tar_path: Path, output_zip: Path) -> Path:
    if tar_path.stat().st_size > MAX_TAR_BYTES:
        raise ValueError("ADB capture stream exceeds the 528 MiB transport limit.")
    if output_zip.exists():
        if output_zip.is_symlink():
            raise ValueError("Private AOT ZIP output may not be a symlink.")
        output_zip.unlink()
    output_zip.parent.mkdir(parents=True, exist_ok=True)

    seen: set[str] = set()
    count = 0
    total = 0
    found_payload = False
    try:
        with tarfile.open(tar_path, mode="r:*") as source, zipfile.ZipFile(
            output_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1
        ) as destination:
            for member in source:
                relative = _safe_relative_capture(member.name)
                if member.isdir():
                    continue
                if not member.isfile() or member.issym() or member.islnk():
                    raise ValueError(
                        f"Non-regular AOT capture entry is not accepted: {member.name}"
                    )
                if relative is None:
                    raise ValueError(f"Unexpected regular file in capture root: {member.name}")
                if relative in seen:
                    raise ValueError(f"Duplicate private AOT capture: {relative}")
                seen.add(relative)
                count += 1
                total += member.size
                if count > MAX_FILES or total > MAX_ARCHIVE_BYTES:
                    raise ValueError("Private AOT captures exceed the 8192-file / 512 MiB limit.")
                if relative.endswith(".bin"):
                    found_payload = True

                stream = source.extractfile(member)
                if stream is None:
                    raise ValueError(f"Could not read private AOT capture: {member.name}")
                info = zipfile.ZipInfo(relative)
                info.compress_type = zipfile.ZIP_DEFLATED
                copied = 0
                with stream, destination.open(info, "w") as target:
                    while True:
                        chunk = stream.read(1024 * 1024)
                        if not chunk:
                            break
                        copied += len(chunk)
                        if copied > member.size:
                            raise ValueError(f"Capture grew during transfer: {relative}")
                        target.write(chunk)
                if copied != member.size:
                    raise ValueError(f"Capture was truncated during transfer: {relative}")

            if not found_payload:
                raise NoDeviceCaptures(
                    "The Android app has not created any VU1/IOP binary captures yet."
                )
            manifest = (
                "format=gow-native-aot-captures-v1\n"
                f"files={count}\n"
                f"captured_bytes={total}\n"
                "source=android-private-files-via-adb\n"
                "private_user_data=true\n"
                "Do not upload this archive to public repositories or CI.\n"
            )
            destination.writestr("capture_manifest.txt", manifest)
    except Exception:
        try:
            output_zip.unlink()
        except FileNotFoundError:
            pass
        raise
    return output_zip


def _select_device(adb: str, serial: str | None) -> str:
    result = subprocess.run(
        [adb, "devices"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise NoDeviceCaptures("ADB could not enumerate Android devices.")
    devices: list[str] = []
    for line in result.stdout.splitlines()[1:]:
        fields = line.strip().split()
        if len(fields) >= 2 and fields[1] == "device":
            devices.append(fields[0])
    if serial:
        if serial not in devices:
            raise NoDeviceCaptures(f"ADB device {serial!r} is not connected or authorized.")
        return serial
    if not devices:
        raise NoDeviceCaptures("No authorized Android device is connected through ADB.")
    if len(devices) > 1:
        raise NoDeviceCaptures(
            "More than one Android device is connected; specify --device-serial."
        )
    return devices[0]


def find_adb_executable() -> str | None:
    """Find ADB on PATH or in the usual Android SDK platform-tools folders."""
    on_path = shutil.which("adb")
    if on_path:
        return on_path

    candidates: list[Path] = []
    for variable in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        value = os.environ.get(variable)
        if value:
            candidates.append(Path(value) / "platform-tools")

    home = Path.home()
    candidates.extend([
        home / "AppData/Local/Android/Sdk/platform-tools",
        home / "Android/Sdk/platform-tools",
        home / "Library/Android/sdk/platform-tools",
    ])
    executable_names = ("adb.exe", "adb") if os.name == "nt" else ("adb",)
    for directory in candidates:
        for executable_name in executable_names:
            candidate = directory / executable_name
            if candidate.is_file():
                return str(candidate)
    return None


def pull_private_aot_captures(
    work_dir: Path,
    repo_root: Path,
    *,
    adb_executable: str | None = None,
    device_serial: str | None = None,
    package_name: str = DEFAULT_PACKAGE,
) -> Path:
    """Pull the app-private capture directory and package it for the AOT generator."""
    root = repo_root.expanduser().resolve()
    work = work_dir.expanduser().resolve()
    try:
        work.relative_to(root)
    except ValueError:
        pass
    else:
        raise ValueError("The private ADB workspace must be outside the public repository.")
    if not str(work).isascii():
        raise ValueError("The private ADB workspace path must use ASCII characters.")

    adb = adb_executable or find_adb_executable()
    if not adb:
        raise NoDeviceCaptures("ADB was not found; install Android SDK Platform Tools or connect its PATH.")
    serial = _select_device(adb, device_serial)

    tar_path = work / "android-aot-captures.tar"
    zip_path = work / "android-aot-captures.zip"
    if tar_path.exists():
        if tar_path.is_symlink():
            raise ValueError("Private ADB transfer path may not be a symlink.")
        tar_path.unlink()
    work.mkdir(parents=True, exist_ok=True)

    if not package_name or any(
        not (character.isalnum() or character in "_.")
        for character in package_name
    ):
        raise ValueError("Android package name contains unsupported characters.")
    remote_command = (
        f'run-as {package_name} sh -c "cd files && toybox tar -cf - gow-aot-captures"'
    )
    command = [adb, "-s", serial, "exec-out", remote_command]
    print("$ " + " ".join(command), flush=True)
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert process.stdout is not None
    size = 0
    try:
        with tar_path.open("xb") as output:
            while True:
                chunk = process.stdout.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_TAR_BYTES:
                    process.kill()
                    raise ValueError("ADB capture stream exceeded the 528 MiB transport limit.")
                output.write(chunk)
        stderr = process.stderr.read() if process.stderr is not None else b""
        return_code = process.wait()
        if return_code != 0:
            detail = stderr.decode("utf-8", errors="replace").strip()
            raise NoDeviceCaptures(
                "The private capture directory could not be read from the Android app"
                + (f": {detail}" if detail else ".")
            )
        if size == 0:
            raise NoDeviceCaptures("The Android app has not created a private AOT capture archive.")
        archive = capture_tar_to_zip(tar_path, zip_path)
        print(f"Private Android AOT captures synchronized: {archive} ({size} transport bytes)")
        return archive
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        try:
            tar_path.unlink()
        except FileNotFoundError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", required=True)
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--adb", default=None)
    parser.add_argument("--device-serial", default=None)
    parser.add_argument("--package", default=DEFAULT_PACKAGE)
    args = parser.parse_args()
    archive = pull_private_aot_captures(
        Path(args.work_dir),
        Path(args.repo_root),
        adb_executable=args.adb,
        device_serial=args.device_serial,
        package_name=args.package,
    )
    print(archive)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Safely migrate a private God of War install between Android Debug signing keys.

The existing app is never removed until its private installation has been backed
up, structurally validated, and checked against its SHA-256 integrity manifest.
A verified backup is retained in the private host workspace after migration.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import tarfile
from pathlib import Path, PurePosixPath

from sync_android_aot_captures import (
    DEFAULT_NTSC_INSTALLATION_DIRECTORY,
    DEFAULT_PACKAGE,
    NoDeviceCaptures,
    resolve_adb_device,
)


MAX_BACKUP_BYTES = 16 * 1024 * 1024 * 1024
MAX_INSTALL_BYTES = 16 * 1024 * 1024 * 1024
MAX_INSTALL_FILES = 65540
MAX_MANIFEST_BYTES = 64 * 1024 * 1024
INTEGRITY_HEADER = b"GOW-DISC-INTEGRITY\t1\n"


def _run(command: list[str], *, input_stream=None) -> subprocess.CompletedProcess:
    print("$ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        stdin=input_stream,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(
            f"Command failed with exit code {result.returncode}: "
            + (detail or "no diagnostic was returned")
        )
    return result


def _safe_member_path(name: str) -> PurePosixPath:
    if not name or "\\" in name:
        raise ValueError(f"Unsafe private-install backup path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts:
        raise ValueError(f"Unsafe private-install backup path: {name!r}")
    if any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"Unsafe private-install backup path: {name!r}")
    return path


def _parse_boot2(system_cnf: bytes) -> str:
    text = system_cnf.decode("ascii", errors="strict")
    boot_target = None
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.strip().upper() == "BOOT2":
            boot_target = value.strip()
            break
    if not boot_target:
        raise ValueError("SYSTEM.CNF does not contain BOOT2.")
    normalized = boot_target.replace("\\", "/")
    if ":" in normalized:
        device, normalized = normalized.split(":", 1)
        if device.strip().lower() != "cdrom0":
            raise ValueError("SYSTEM.CNF BOOT2 does not point to cdrom0.")
    parts = []
    for component in normalized.strip("/").split("/"):
        component = component.split(";", 1)[0].upper()
        if not component or component in (".", "..") or len(component) > 255:
            raise ValueError("SYSTEM.CNF contains an unsafe BOOT2 path.")
        if not re.fullmatch(r"[A-Z0-9_.+-]+", component):
            raise ValueError("SYSTEM.CNF BOOT2 contains unsupported path characters.")
        parts.append(component)
    if not parts or parts[-1] != "SCUS_973.99":
        raise ValueError("The installed private disc is not the supported NTSC-U SCUS_973.99 release.")
    return "/".join(parts)


def validate_private_installation_backup(
    archive_path: Path,
    installation_directory: str = DEFAULT_NTSC_INSTALLATION_DIRECTORY,
) -> dict[str, object]:
    """Validate archive paths, required files, BOOT2 and every manifest SHA-256."""
    if archive_path.is_symlink() or not archive_path.is_file():
        raise ValueError("The private installation backup is missing or is a symlink.")
    archive_size = archive_path.stat().st_size
    if archive_size <= 0 or archive_size > MAX_BACKUP_BYTES:
        raise ValueError("The private installation backup exceeds the 16 GiB safety limit.")

    install_prefix = f"installations/{installation_directory}"
    members: dict[str, tarfile.TarInfo] = {}
    total_size = 0
    file_count = 0
    saw_installation_root = False
    saw_capture_root = False

    with tarfile.open(archive_path, mode="r:*") as archive:
        for member in archive:
            path = _safe_member_path(member.name)
            parts = path.parts
            if parts[0] == "installations":
                if len(parts) < 2 or parts[1] != installation_directory:
                    raise ValueError(f"Unexpected installation tree entry: {member.name}")
                if len(parts) == 2:
                    saw_installation_root = member.isdir()
                allowed = True
            elif parts[0] == "gow-aot-captures":
                saw_capture_root = True
                allowed = True
            else:
                allowed = False
            if not allowed:
                raise ValueError(f"Unexpected private backup entry: {member.name}")

            if member.isdir():
                continue
            if not member.isfile() or member.issym() or member.islnk():
                raise ValueError(f"Non-regular private backup entry is not accepted: {member.name}")
            if member.name in members:
                raise ValueError(f"Duplicate private backup entry: {member.name}")
            members[member.name] = member
            file_count += 1
            total_size += member.size
            if file_count > MAX_INSTALL_FILES or total_size > MAX_INSTALL_BYTES:
                raise ValueError("The private installation exceeds its file-count or 16 GiB limit.")

        required = {
            f"{install_prefix}/disc/SYSTEM.CNF",
            f"{install_prefix}/disc/.disc_layout",
            f"{install_prefix}/disc/.disc_meta",
            f"{install_prefix}/disc/.gow_disc_integrity",
        }
        missing = sorted(required.difference(members))
        if missing:
            raise ValueError("The private backup is incomplete: " + ", ".join(missing))

        system_member = members[f"{install_prefix}/disc/SYSTEM.CNF"]
        system_stream = archive.extractfile(system_member)
        if system_stream is None or system_member.size > 64 * 1024:
            raise ValueError("Could not read the bounded SYSTEM.CNF backup.")
        with system_stream:
            boot_path = _parse_boot2(system_stream.read(64 * 1024 + 1))
        boot_member_path = f"{install_prefix}/disc/{boot_path}"
        if boot_member_path not in members:
            raise ValueError(f"The BOOT2 executable is missing from the backup: {boot_path}")

        manifest_member = members[f"{install_prefix}/disc/.gow_disc_integrity"]
        if manifest_member.size > MAX_MANIFEST_BYTES:
            raise ValueError("The SHA-256 integrity manifest exceeds 64 MiB.")
        manifest_stream = archive.extractfile(manifest_member)
        if manifest_stream is None:
            raise ValueError("Could not read the SHA-256 integrity manifest.")
        with manifest_stream:
            manifest_bytes = manifest_stream.read(MAX_MANIFEST_BYTES + 1)
        if len(manifest_bytes) > MAX_MANIFEST_BYTES or not manifest_bytes.startswith(INTEGRITY_HEADER):
            raise ValueError("The SHA-256 integrity manifest header is invalid.")

        expected: dict[str, tuple[int, str]] = {}
        for line_number, raw_line in enumerate(
            manifest_bytes[len(INTEGRITY_HEADER):].splitlines(), start=2
        ):
            if not raw_line:
                continue
            fields = raw_line.decode("utf-8", errors="strict").split("\t", 2)
            if len(fields) != 3:
                raise ValueError(f"Malformed SHA-256 integrity entry at line {line_number}.")
            digest, size_text, relative = fields
            _safe_member_path(relative)
            if not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
                raise ValueError(f"Invalid SHA-256 digest at line {line_number}.")
            try:
                expected_size = int(size_text, 10)
            except ValueError as error:
                raise ValueError(f"Invalid file size at line {line_number}.") from error
            tar_name = f"{install_prefix}/disc/{relative}"
            if expected_size < 0 or tar_name not in members or tar_name in expected:
                raise ValueError(f"Integrity manifest references a missing or duplicate file: {relative}")
            if members[tar_name].size != expected_size:
                raise ValueError(f"Backup size differs from the integrity manifest: {relative}")
            expected[tar_name] = (expected_size, digest.lower())

        disc_files = {
            name for name in members
            if name.startswith(f"{install_prefix}/disc/")
            and name != f"{install_prefix}/disc/.gow_disc_integrity"
        }
        if set(expected) != disc_files:
            raise ValueError("The backup file set does not match the SHA-256 integrity manifest.")

        hashed_bytes = 0
        for name, (expected_size, expected_digest) in expected.items():
            member = members[name]
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"Could not read backup file for SHA-256 verification: {name}")
            digest = hashlib.sha256()
            copied = 0
            with stream:
                while True:
                    chunk = stream.read(1024 * 1024)
                    if not chunk:
                        break
                    copied += len(chunk)
                    digest.update(chunk)
            if copied != expected_size or digest.hexdigest() != expected_digest:
                raise ValueError(f"SHA-256 verification failed in backup: {name}")
            hashed_bytes += copied

    return {
        "installation_directory": installation_directory,
        "boot_relative_path": boot_path,
        "file_count": file_count,
        "total_bytes": total_size,
        "verified_disc_bytes": hashed_bytes,
        "aot_captures_included": saw_capture_root,
        "backup_sha256": _sha256_file(archive_path),
    }


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _pull_private_installation_backup(
    adb: str, serial: str, package_name: str, work_dir: Path,
    installation_directory: str,
) -> Path:
    destination = work_dir / "android-signature-migration" / "private-disc-backup.tar"
    if destination.parent.exists() and destination.parent.is_symlink():
        raise ValueError("The private migration workspace may not be a symlink.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(
            f"Refusing to overwrite an existing private backup: {destination}"
        )
    remote = (
        f'run-as {package_name} sh -c "cd files && '
        f'toybox tar -cf - installations/{installation_directory}"'
    )
    command = [adb, "-s", serial, "exec-out", remote]
    print("$ " + " ".join(command), flush=True)
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert process.stdout is not None
    total = 0
    digest = hashlib.sha256()
    try:
        with destination.open("xb") as output:
            while True:
                chunk = process.stdout.read(4 * 1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_BACKUP_BYTES:
                    process.kill()
                    raise ValueError("Private installation backup exceeds 16 GiB.")
                output.write(chunk)
                digest.update(chunk)
        stderr = process.stderr.read() if process.stderr is not None else b""
        return_code = process.wait()
        if return_code != 0:
            detail = stderr.decode("utf-8", errors="replace").strip()
            raise NoDeviceCaptures(
                "Could not back up the private Android installation"
                + (f": {detail}" if detail else ".")
            )
        if total == 0:
            raise NoDeviceCaptures("The Android app returned an empty installation backup.")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    print(f"Private installation backup received: {destination} ({total} bytes)")
    print(f"Transfer SHA-256: {digest.hexdigest()}")
    return destination


def _pull_installed_apks(
    adb: str, serial: str, package_name: str, backup_directory: Path
) -> list[Path]:
    result = _run([adb, "-s", serial, "shell", "pm", "path", package_name])
    remote_paths = []
    for line in result.stdout.decode("utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line.startswith("package:"):
            continue
        remote = line[len("package:"):]
        name = PurePosixPath(remote).name
        if not re.fullmatch(r"[A-Za-z0-9._-]+\.apk", name):
            raise ValueError(f"Unexpected installed APK path: {remote}")
        remote_paths.append((remote, name))
    if not remote_paths or len(remote_paths) > 8:
        raise RuntimeError("Could not identify a bounded set of installed APKs for rollback.")

    apk_directory = backup_directory / "original-apk"
    apk_directory.mkdir(parents=True, exist_ok=True)
    backups = []
    for remote, name in remote_paths:
        destination = apk_directory / name
        _run([adb, "-s", serial, "pull", remote, str(destination)])
        if not destination.is_file() or destination.stat().st_size <= 0:
            raise RuntimeError(f"Original installed APK backup is incomplete: {name}")
        backups.append(destination)
    return backups


def _probe_run_as_and_stdin(adb: str, serial: str, package_name: str) -> None:
    _run([adb, "-s", serial, "shell", "run-as", package_name, "id"])
    # Verify before uninstalling anything that the ADB shell forwards stdin to
    # a run-as child process. The probe writes no device files.
    probe_command = (
        f'run-as {package_name} sh -c "cat >/dev/null"'
    )
    probe = subprocess.run(
        [adb, "-s", serial, "shell", probe_command],
        input=b"gow-migration-probe",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if probe.returncode != 0:
        detail = probe.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(
            "This ADB version/device cannot stream a private restore safely; "
            "the current app was not removed. "
            + detail
        )


def _restore_private_installation(
    adb: str, serial: str, package_name: str, archive_path: Path,
    installation_directory: str, boot_relative_path: str,
) -> None:
    _run([
        adb, "-s", serial, "shell",
        f"run-as {package_name} mkdir -p files/installations",
    ])
    remote = (
        f'run-as {package_name} sh -c "cd files && toybox tar -xf -"'
    )
    with archive_path.open("rb") as archive:
        _run([adb, "-s", serial, "shell", remote], input_stream=archive)

    boot_path = _safe_member_path(boot_relative_path).as_posix()
    if PurePosixPath(boot_path).name != "SCUS_973.99":
        raise ValueError("The restored BOOT2 path is not the supported NTSC-U executable.")
    verify_script = (
        f"test -s files/installations/{installation_directory}/disc/SYSTEM.CNF"
        f" && test -s files/installations/{installation_directory}/disc/{boot_path}"
        f" && test -s files/installations/{installation_directory}/disc/.disc_layout"
        f" && test -s files/installations/{installation_directory}/disc/.disc_meta"
        f" && test -s files/installations/{installation_directory}/disc/.gow_disc_integrity"
    )
    verified = _run([
        adb, "-s", serial, "shell",
        f'run-as {package_name} sh -c "{verify_script}"',
    ])
    _ = verified


def _install_apks(
    adb: str, serial: str, apk_paths: list[Path], *, multiple: bool = False
) -> None:
    command = [adb, "-s", serial, "install-multiple"] if multiple else [adb, "-s", serial, "install"]
    command.extend(str(path) for path in apk_paths)
    _run(command)


def migrate_debug_installation(
    *,
    apk_path: Path,
    work_dir: Path,
    repo_root: Path,
    device_serial: str | None = None,
    package_name: str = DEFAULT_PACKAGE,
    installation_directory: str = DEFAULT_NTSC_INSTALLATION_DIRECTORY,
) -> Path:
    """Back up, replace and restore the current Debug package without losing its disc."""
    repo = repo_root.expanduser().resolve()
    work = work_dir.expanduser().resolve()
    try:
        work.relative_to(repo)
    except ValueError:
        pass
    else:
        raise ValueError("The private migration workspace must be outside the public repository.")
    if not str(work).isascii():
        raise ValueError("The private migration workspace path must use ASCII characters.")
    if apk_path.is_symlink() or not apk_path.is_file():
        raise ValueError("The newly built Android APK is missing or is a symlink.")

    adb, serial = resolve_adb_device(device_serial=device_serial)
    backup_directory = work / "android-signature-migration"
    if backup_directory.exists() and backup_directory.is_symlink():
        raise ValueError("The private migration workspace may not be a symlink.")
    backup_directory.mkdir(parents=True, exist_ok=True)

    # Everything through this point is read-only with respect to the installed app.
    _probe_run_as_and_stdin(adb, serial, package_name)
    old_apks = _pull_installed_apks(adb, serial, package_name, backup_directory)
    backup_tar = _pull_private_installation_backup(
        adb, serial, package_name, backup_directory, installation_directory
    )
    summary = validate_private_installation_backup(backup_tar, installation_directory)
    summary_path = backup_directory / "backup-verification.txt"
    summary_path.write_text(
        "\n".join(f"{key}={value}" for key, value in summary.items()) + "\n",
        encoding="utf-8",
    )
    print("Private installation backup passed path, BOOT2 and SHA-256 validation.")
    print(f"Original APK rollback copies: {len(old_apks)}")
    print("The existing app will now be replaced; its verified private disc backup is retained.")

    uninstalled = False
    try:
        _run([adb, "-s", serial, "uninstall", package_name])
        uninstalled = True
        _install_apks(adb, serial, [apk_path])
        _restore_private_installation(
            adb, serial, package_name, backup_tar, installation_directory,
            str(summary["boot_relative_path"]),
        )
    except Exception as migration_error:
        if not uninstalled:
            raise
        print(f"Migration failed: {migration_error}")
        print("Attempting automatic rollback to the original APK and private disc...")
        try:
            subprocess.run(
                [adb, "-s", serial, "uninstall", package_name],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            _install_apks(adb, serial, old_apks, multiple=len(old_apks) > 1)
            _restore_private_installation(
                adb, serial, package_name, backup_tar, installation_directory,
                str(summary["boot_relative_path"]),
            )
        except Exception as rollback_error:
            raise RuntimeError(
                "APK migration and automatic rollback both failed. "
                f"The verified private backup remains at {backup_tar}. "
                f"Migration error: {migration_error}; rollback error: {rollback_error}"
            ) from migration_error
        raise RuntimeError(
            "The new APK was not kept. The previous APK and private disc were restored. "
            f"Verified backup retained at {backup_tar}. Original error: {migration_error}"
        ) from migration_error

    print("Debug signing migration completed; private disc installation was restored.")
    print(f"Keep the host Debug keystore and the verified backup at: {backup_tar}")
    return backup_tar


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--device-serial", default=None)
    args = parser.parse_args()
    migrate_debug_installation(
        apk_path=args.apk,
        work_dir=args.work_dir,
        repo_root=Path(args.repo_root),
        device_serial=args.device_serial,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

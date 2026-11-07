#!/usr/bin/env python3
"""Phone-side Termux bridge for the private Codespaces AOT iteration loop.

Termux uses Android Wireless debugging to connect ADB back to the same phone.
No ELF, IRX, ISO or capture file is manually exported by the user.
"""
from __future__ import annotations

import argparse
import http.client
import ipaddress
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
from urllib.parse import urlsplit

from sync_android_aot_captures import (
    DEFAULT_PACKAGE,
    NoDeviceCaptures,
    pull_private_aot_captures,
)


def adb_executable() -> str:
    adb = shutil.which("adb")
    if not adb:
        raise RuntimeError(
            "ADB is not installed in Termux. Install the official Termux package: "
            "pkg install android-tools"
        )
    return adb


def run(command: list[str], *, allow_failure: bool = False) -> subprocess.CompletedProcess[str]:
    print("$ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
    if result.returncode != 0 and not allow_failure:
        raise RuntimeError(f"Command failed with exit code {result.returncode}.")
    return result


def connect_to_phone(
    adb: str,
    host_ip: str,
    pairing_port: int,
    pairing_code: str,
    connection_port: int,
) -> str:
    try:
        device_ip = ipaddress.ip_address(host_ip)
    except ValueError as error:
        raise ValueError("Use the IP address shown by Android Wireless debugging.") from error
    if device_ip.version != 4 or device_ip.is_loopback or device_ip.is_unspecified:
        raise ValueError("Wireless debugging must use the phone's displayed IPv4 address, not localhost.")
    if not 1 <= pairing_port <= 65535 or not 1 <= connection_port <= 65535:
        raise ValueError("ADB ports must be in the range 1–65535.")
    if not pairing_code.isdigit() or len(pairing_code) < 6:
        raise ValueError("Wireless debugging pairing code must contain at least six digits.")

    pair = run(
        [adb, "pair", f"{device_ip}:{pairing_port}", pairing_code],
        allow_failure=True,
    )
    if pair.returncode != 0 and "already paired" not in (pair.stdout or "").lower():
        raise RuntimeError(
            "ADB pairing failed. Keep Developer options > Wireless debugging open, "
            "use the pairing port/code (not the connection port), and try again."
        )
    connect = run(
        [adb, "connect", f"{device_ip}:{connection_port}"],
        allow_failure=True,
    )
    if connect.returncode != 0 or not any(
        marker in (connect.stdout or "").lower()
        for marker in ("connected to", "already connected")
    ):
        raise RuntimeError(
            "ADB could not connect to this phone over localhost. This self-connect "
            "method depends on the device's Android/ColorOS wireless-debugging implementation."
        )

    devices = run([adb, "devices"])
    connected = [
        line.split()[0]
        for line in (devices.stdout or "").splitlines()[1:]
        if len(line.split()) >= 2 and line.split()[1] == "device"
    ]
    if not connected:
        raise RuntimeError("ADB paired, but no authorized device is available.")
    if len(connected) != 1:
        raise RuntimeError(
            "More than one ADB device is connected. Disconnect the others or pass a serial explicitly."
        )
    serial = connected[0]
    run([adb, "-s", serial, "shell", "run-as", DEFAULT_PACKAGE, "id"])
    print(f"Phone ADB session ready: {serial}")
    print("The debug app's private installation can now be read through run-as.")
    return serial


def _http_connection(parsed):
    if parsed.scheme != "https":
        raise ValueError("Codespaces phone-builder URL must use HTTPS.")
    if not parsed.hostname:
        raise ValueError("Codespaces phone-builder URL has no hostname.")
    connection = http.client.HTTPSConnection(
        parsed.hostname,
        parsed.port or 443,
        timeout=60,
        context=ssl.create_default_context(),
    )
    return connection


def upload_capture_archive(
    archive: Path,
    builder_url: str,
    token: str,
) -> dict[str, object]:
    if archive.is_symlink() or not archive.is_file() or archive.stat().st_size <= 0:
        raise ValueError("The private AOT capture archive is missing or unsafe.")
    if archive.stat().st_size > 512 * 1024 * 1024:
        raise ValueError("The private AOT capture archive exceeds 512 MiB.")
    parsed = urlsplit(builder_url.rstrip("/"))
    connection = _http_connection(parsed)
    target = (parsed.path.rstrip("/") or "") + "/api/captures"
    size = archive.stat().st_size
    try:
        connection.putrequest("POST", target)
        connection.putheader("Authorization", f"Bearer {token}")
        connection.putheader("Content-Type", "application/zip")
        connection.putheader("Content-Length", str(size))
        connection.endheaders()
        with archive.open("rb") as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                connection.send(chunk)
        response = connection.getresponse()
        payload = response.read(64 * 1024)
        if response.status not in (200, 201):
            try:
                detail = json.loads(payload.decode("utf-8")).get("message", payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError, AttributeError):
                detail = payload.decode("utf-8", errors="replace")
            raise RuntimeError(f"Codespaces capture upload failed ({response.status}): {detail}")
        result = json.loads(payload.decode("utf-8"))
        return result
    finally:
        connection.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="local Gow_android checkout cloned in Termux",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path.home() / ".cache/gow-phone-bridge",
        help="private temporary Termux workspace",
    )
    commands = parser.add_subparsers(dest="command", required=True)

    pair = commands.add_parser("connect", help="pair Termux ADB with this phone's Wireless debugging")
    pair.add_argument(
        "--host-ip",
        required=True,
        help="IPv4 address shown by Android Wireless debugging (do not use 127.0.0.1)",
    )
    pair.add_argument("--pairing-port", required=True, type=int)
    pair.add_argument("--code", required=True)
    pair.add_argument("--connection-port", required=True, type=int)

    upload = commands.add_parser("upload-captures", help="sync private app captures directly to Codespaces")
    upload.add_argument("--url", required=True, help="HTTPS URL for the forwarded Codespaces port")
    upload.add_argument("--token", required=True, help="private token printed by phone_only_builder.py")
    upload.add_argument("--device-serial", default=None)

    install = commands.add_parser("install-apk", help="replace the Debug APK without losing the installed ISO")
    install.add_argument("--apk", required=True, type=Path)
    install.add_argument(
        "--device-serial",
        default=None,
        help="optional ADB serial if the phone is not the only connected device",
    )

    args = parser.parse_args()
    repo = args.repo_root.expanduser().resolve()
    if not (repo / "tools/sync_android_aot_captures.py").is_file():
        parser.error("--repo-root must point to a Gow_android checkout.")
    work = args.work_dir.expanduser().resolve()
    if work == repo or work.is_relative_to(repo):
        parser.error("Termux bridge workspace must remain outside the Git repository.")
    work.mkdir(parents=True, exist_ok=True)

    adb = adb_executable()
    if args.command == "connect":
        connect_to_phone(adb, args.host_ip, args.pairing_port, args.code, args.connection_port)
        return 0

    if args.command == "upload-captures":
        serial = args.device_serial
        if serial is None:
            devices = run([adb, "devices"])
            connected = [
                line.split()[0]
                for line in (devices.stdout or "").splitlines()[1:]
                if len(line.split()) >= 2 and line.split()[1] == "device"
            ]
            if len(connected) != 1:
                raise RuntimeError("Connect ADB to this phone first; expected exactly one authorized device.")
            serial = connected[0]
        archive = pull_private_aot_captures(
            work,
            repo,
            adb_executable=adb,
            device_serial=serial,
            package_name=DEFAULT_PACKAGE,
        )
        try:
            result = upload_capture_archive(archive, args.url, args.token)
            print(
                "Private AOT captures uploaded to Codespaces: "
                f"{result.get('bytes')} bytes · SHA-256 {result.get('sha256')}"
            )
        finally:
            try:
                archive.unlink()
            except FileNotFoundError:
                pass
        print("No manual ELF, IRX or capture export was required.")
        return 0

    if args.command == "install-apk":
        apk_candidate = args.apk.expanduser()
        if apk_candidate.is_symlink():
            parser.error(f"APK file may not be a symbolic link: {apk_candidate}")
        apk = apk_candidate.resolve()
        if not apk.is_file():
            parser.error(f"APK file not found: {apk}")
        migration = repo / "tools/migrate_android_debug_installation.py"
        command = [
            sys.executable,
            str(migration),
            "--apk",
            str(apk),
            "--work-dir",
            str(work / "signature-migration"),
            "--repo-root",
            str(repo),
        ]
        if args.device_serial:
            command.extend(["--device-serial", args.device_serial])
        run(command)
        print("Debug APK updated. The private ISO installation was backed up, verified and restored.")
        return 0

    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, NoDeviceCaptures) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

#!/usr/bin/env python3
"""Host tests for the authenticated phone-only Codespaces builder."""
from __future__ import annotations

from http.client import HTTPConnection
import io
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
import zipfile
from http.server import ThreadingHTTPServer

from phone_only_builder import (
    BuilderState,
    MAX_CAPTURE_BYTES,
    MAX_ISO_BYTES,
    make_handler,
    validate_private_work_directory,
)


class PhoneOnlyBuilderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gow-phone-builder-test-")
        base = Path(self.temporary.name)
        self.repo = base / "repo"
        self.work = base / "private-work"
        (self.repo / "tools").mkdir(parents=True)
        (self.repo / "tools/build_android_game.py").write_text(
            "from pathlib import Path\n"
            "import sys\n"
            "root = Path(__file__).resolve().parents[1]\n"
            "apk = root / 'android/app/build/outputs/apk/debug/app-debug.apk'\n"
            "apk.parent.mkdir(parents=True, exist_ok=True)\n"
            "apk.write_bytes(b'fake-arm64-apk')\n"
            "print('phone builder test APK generated', flush=True)\n",
            encoding="utf-8",
        )
        self.state = BuilderState(self.repo, self.work)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.state))
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)

    def tearDown(self) -> None:
        self.connection.close()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.temporary.cleanup()

    def request(self, method: str, path: str, body: bytes = b"", *, authorized: bool = True,
                content_type: str = "application/octet-stream"):
        headers = {"Content-Length": str(len(body)), "Content-Type": content_type}
        if authorized:
            headers["Authorization"] = "Bearer " + self.state.token
        self.connection.request(method, path, body=body, headers=headers)
        response = self.connection.getresponse()
        payload = response.read()
        return response.status, payload

    def test_workspace_must_be_private_and_outside_checkout(self) -> None:
        with self.assertRaises(ValueError):
            validate_private_work_directory(self.repo, self.repo / "android/app/build")
        accepted = validate_private_work_directory(self.repo, self.work)
        self.assertEqual(accepted, self.work.resolve())

    def test_upload_authentication_and_private_storage(self) -> None:
        status, _ = self.request("POST", "/api/iso", b"iso-data", authorized=False)
        self.assertEqual(status, 401)

        status, payload = self.request("POST", "/api/iso", b"iso-data")
        self.assertEqual(status, 201)
        result = json.loads(payload)
        self.assertEqual(result["kind"], "iso")
        self.assertEqual(self.state.iso_path.read_bytes(), b"iso-data")
        self.assertFalse(str(self.state.iso_path).startswith(str(self.repo)))
        self.assertEqual(len(result["sha256"]), 64)

    def test_invalid_capture_zip_is_rejected(self) -> None:
        status, _ = self.request("POST", "/api/captures", b"not-a-zip")
        self.assertEqual(status, 400)
        self.assertFalse(self.state.captures_path.exists())

    def test_build_and_download_requires_completed_success(self) -> None:
        self.state.upload(kind="iso", body=io.BytesIO(b"original-iso"), content_length=12)
        capture_buffer = io.BytesIO()
        with zipfile.ZipFile(capture_buffer, "w") as archive:
            archive.writestr("vu1/vu1n_heads.txt", "0000\n")
        capture_data = capture_buffer.getvalue()
        self.state.upload(
            kind="captures",
            body=io.BytesIO(capture_data),
            content_length=len(capture_data),
        )

        status, _ = self.request("GET", "/api/apk")
        self.assertEqual(status, 409)

        status, payload = self.request("POST", "/api/build", b"{}", content_type="application/json")
        self.assertEqual(status, 202)
        self.assertIn("AOT iteration", json.loads(payload)["mode"])

        deadline = time.time() + 10
        current = None
        while time.time() < deadline:
            status, payload = self.request("GET", "/api/status")
            self.assertEqual(status, 200)
            current = json.loads(payload)
            if current["status"] in ("succeeded", "failed"):
                break
            time.sleep(0.05)
        self.assertIsNotNone(current)
        self.assertEqual(current["status"], "succeeded")
        self.assertGreater(current["apk_bytes"], 0)

        status, payload = self.request("GET", "/api/apk")
        self.assertEqual(status, 200)
        self.assertEqual(payload, b"fake-arm64-apk")

    def test_upload_limits_reject_before_reading_body(self) -> None:
        with self.assertRaises(ValueError):
            self.state.upload(kind="iso", body=io.BytesIO(), content_length=MAX_ISO_BYTES + 1)
        with self.assertRaises(ValueError):
            self.state.upload(kind="captures", body=io.BytesIO(), content_length=MAX_CAPTURE_BYTES + 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""Authenticated phone-friendly HTTP front end for a private Codespaces build.

The ISO and AOT capture ZIP are stored outside the Git checkout. The server
accepts data uploads only; it never accepts shell commands or arbitrary paths.
"""
from __future__ import annotations

import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import threading
import time
import zipfile
from typing import Any

MAX_ISO_BYTES = 8 * 1024 * 1024 * 1024
MAX_CAPTURE_BYTES = 512 * 1024 * 1024
MAX_JSON_BYTES = 4096
APK_RELATIVE_PATH = Path("android/app/build/outputs/apk/debug/app-debug.apk")
DEFAULT_PORT = 8765


def is_within(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def validate_private_work_directory(repo_root: Path, work_dir: Path) -> Path:
    root = repo_root.expanduser().resolve()
    candidate = work_dir.expanduser()
    if candidate.is_symlink():
        raise ValueError("Phone builder workspace may not be a symbolic link.")
    work = candidate.resolve()
    if work == root or is_within(work, root):
        raise ValueError("Phone builder workspace must remain outside the Git repository.")
    if is_within(root, work):
        raise ValueError("Phone builder workspace may not contain the Git repository.")
    work.mkdir(parents=True, exist_ok=True)
    if work.is_symlink():
        raise ValueError("Phone builder workspace may not be a symbolic link.")
    return work


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class BuilderState:
    def __init__(self, repo_root: Path, work_dir: Path) -> None:
        self.repo_root = repo_root.expanduser().resolve()
        if not (self.repo_root / "tools/build_android_game.py").is_file():
            raise ValueError("Run this server from the Gow_android repository checkout.")
        self.work_dir = validate_private_work_directory(self.repo_root, work_dir)
        self.input_dir = self.work_dir / "input"
        self.input_dir.mkdir(parents=True, exist_ok=True)
        self.iso_path = self.input_dir / "original-god-of-war.iso"
        self.captures_path = self.input_dir / "android-aot-captures.zip"
        self.log_path = self.work_dir / "build.log"
        self.token_path = self.work_dir / "bridge-token.txt"
        self.lock = threading.RLock()
        self.process: subprocess.Popen[bytes] | None = None
        self.build_started_at: float | None = None
        self.build_mode: str | None = None
        self.last_returncode: int | None = None
        self.last_error: str | None = None
        self.last_iso_sha256: str | None = None
        self.last_capture_sha256: str | None = None
        self.token = self._load_or_create_token()

    def _load_or_create_token(self) -> str:
        if self.token_path.is_file() and not self.token_path.is_symlink():
            token = self.token_path.read_text(encoding="ascii").strip()
            if len(token) >= 40:
                return token
        token = secrets.token_urlsafe(36)
        temporary = self.token_path.with_suffix(".tmp")
        temporary.write_text(token + "\n", encoding="ascii")
        os.chmod(temporary, 0o600)
        temporary.replace(self.token_path)
        os.chmod(self.token_path, 0o600)
        return token

    def build_running(self) -> bool:
        return self.process is not None and self.process.poll() is None

    def upload(self, *, kind: str, body: Any, content_length: int) -> dict[str, Any]:
        if kind == "iso":
            limit = MAX_ISO_BYTES
            destination = self.iso_path
        elif kind == "captures":
            limit = MAX_CAPTURE_BYTES
            destination = self.captures_path
        else:
            raise ValueError("Unsupported upload kind.")
        if content_length <= 0 or content_length > limit:
            raise ValueError(f"Upload must contain between 1 byte and {limit} bytes.")
        with self.lock:
            if self.build_running():
                raise RuntimeError("A build is running; wait before replacing private inputs.")
            temporary = destination.with_name(destination.name + ".partial")
            if temporary.exists() or temporary.is_symlink():
                if temporary.is_symlink():
                    raise ValueError("Refusing to replace a symbolic-link upload.")
                temporary.unlink()
            digest = hashlib.sha256()
            copied = 0
            try:
                with temporary.open("xb") as output:
                    while copied < content_length:
                        chunk = body.read(min(1024 * 1024, content_length - copied))
                        if not chunk:
                            raise ValueError("Upload ended before Content-Length bytes arrived.")
                        copied += len(chunk)
                        if copied > limit:
                            raise ValueError("Upload exceeded its configured size limit.")
                        output.write(chunk)
                        digest.update(chunk)
                if kind == "captures" and not zipfile.is_zipfile(temporary):
                    raise ValueError("AOT capture upload is not a valid ZIP archive.")
                temporary.replace(destination)
            except Exception:
                try:
                    temporary.unlink()
                except FileNotFoundError:
                    pass
                raise
            checksum = digest.hexdigest()
            if kind == "iso":
                self.last_iso_sha256 = checksum
            else:
                self.last_capture_sha256 = checksum
            return {"kind": kind, "bytes": copied, "sha256": checksum}

    def start_build(self) -> dict[str, Any]:
        with self.lock:
            if self.build_running():
                raise RuntimeError("A Codespaces build is already running.")
            if not self.iso_path.is_file() or self.iso_path.is_symlink():
                raise RuntimeError("Upload the original God of War ISO first.")
            command = [
                sys.executable,
                str(self.repo_root / "tools/build_android_game.py"),
                "--iso",
                str(self.iso_path),
                "--work-dir",
                str(self.work_dir / "recompiler-work"),
            ]
            mode = "strict capture bootstrap"
            if self.captures_path.is_file():
                command.extend(["--aot-captures", str(self.captures_path)])
                mode = "AOT iteration from private Android captures"
            environment = os.environ.copy()
            environment.setdefault("GOW_BUILD_JOBS", "2")
            environment["PYTHONUNBUFFERED"] = "1"
            log = self.log_path.open("w", encoding="utf-8")
            try:
                self.process = subprocess.Popen(
                    command,
                    cwd=str(self.repo_root),
                    env=environment,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
            except Exception:
                log.close()
                self.process = None
                raise
            # The child inherits the file descriptor. The server does not retain
            # a writable handle, so log polling always reads the flushed file.
            log.close()
            self.build_started_at = time.time()
            self.build_mode = mode
            self.last_returncode = None
            self.last_error = None
            return {"status": "started", "mode": mode, "pid": self.process.pid}

    def status(self) -> dict[str, Any]:
        with self.lock:
            process = self.process
            returncode = process.poll() if process is not None else self.last_returncode
            apk = (self.repo_root / APK_RELATIVE_PATH).resolve()
            if process is not None and returncode is not None:
                self.last_returncode = returncode
            if process is None:
                state = "idle"
            elif returncode is None:
                state = "running"
            elif returncode == 0 and apk.is_file() and not apk.is_symlink():
                state = "succeeded"
            else:
                state = "failed"
            log_tail = ""
            if self.log_path.is_file() and not self.log_path.is_symlink():
                with self.log_path.open("rb") as stream:
                    stream.seek(max(0, self.log_path.stat().st_size - 16000))
                    log_tail = stream.read().decode("utf-8", errors="replace")
            apk_size = apk.stat().st_size if apk.is_file() and not apk.is_symlink() else 0
            return {
                "status": state,
                "mode": self.build_mode,
                "returncode": returncode,
                "started_at": self.build_started_at,
                "iso_uploaded": self.iso_path.is_file(),
                "iso_sha256": self.last_iso_sha256,
                "captures_uploaded": self.captures_path.is_file(),
                "captures_sha256": self.last_capture_sha256,
                "apk_bytes": apk_size,
                "log_tail": log_tail,
                "error": self.last_error,
            }

    def apk_path(self) -> Path:
        with self.lock:
            if self.process is None or self.process.poll() is None:
                raise RuntimeError("The build has not completed.")
            if self.process.returncode != 0:
                raise RuntimeError("The latest build failed; inspect the private build log.")
            candidate = self.repo_root / APK_RELATIVE_PATH
            if candidate.is_symlink():
                raise RuntimeError("Refusing to download an APK symlink.")
            apk = candidate.resolve()
            if not is_within(apk, self.repo_root / "android/app/build"):
                raise RuntimeError("APK path escaped the ignored build directory.")
            if not apk.is_file() or apk.stat().st_size <= 0:
                raise RuntimeError("Gradle completed without a valid Debug APK.")
            return apk


HTML = """<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FORGE — Compilador móvel</title>
<style>
:root{color-scheme:dark;font:16px system-ui,sans-serif;background:#101114;color:#f4f4f5}
body{max-width:760px;margin:0 auto;padding:20px}
main{background:#1a1c21;border:1px solid #343840;border-radius:16px;padding:20px}
h1{margin:0 0 8px;color:#fff}p{line-height:1.5;color:#c5c7cd}
label{display:block;margin:16px 0 8px;font-weight:650}
input,button{box-sizing:border-box;width:100%;padding:13px;border-radius:10px;border:1px solid #454a54;background:#101114;color:#fff}
button{margin-top:12px;background:#c51f32;border:0;font-weight:700}
button.secondary{background:#30343d}
button:disabled{opacity:.5}
pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#0c0d10;padding:12px;border-radius:10px;max-height:42vh;overflow:auto;color:#c9e6c9}
small{color:#a4a8b2}
</style>
</head>
<body><main>
<h1>FORGE · Compilador móvel</h1>
<p>Este ambiente remoto recebe somente a ISO original e, nas iterações seguintes, as capturas AOT privadas do Android. ELF, IRX e C++ gerado são tratados automaticamente e ficam fora do repositório.</p>
<label for="token">Token privado do Codespace</label>
<input id="token" type="password" autocomplete="off" placeholder="Cole o token mostrado no Terminal do Codespace">
<label for="iso">ISO original de God of War (NTSC-U)</label>
<input id="iso" type="file" accept=".iso,application/octet-stream">
<button id="upload">Enviar ISO original</button>
<button id="build">Iniciar / repetir compilação</button>
<button id="refresh" class="secondary">Atualizar estado e log</button>
<button id="download" class="secondary">Baixar APK ARM64</button>
<p id="message" role="status">Aguardando autenticação e ISO.</p>
<pre id="log">O log privado aparecerá aqui depois da primeira compilação.</pre>
<small>Não feche nem exclua este Codespace durante a compilação. O token não é salvo pelo navegador.</small>
</main>
<script>
const token=document.getElementById('token');
const message=document.getElementById('message');
const log=document.getElementById('log');
const auth=()=>({Authorization:'Bearer '+token.value.trim()});
async function api(path,options={}) {
  const response=await fetch(path,{...options,headers:{...auth(),...(options.headers||{})}});
  const type=response.headers.get('content-type')||'';
  const body=type.includes('application/json')?await response.json():await response.text();
  if(!response.ok) throw new Error(body.message||body||('HTTP '+response.status));
  return body;
}
document.getElementById('upload').onclick=async()=>{
  const file=document.getElementById('iso').files[0];
  if(!file){message.textContent='Selecione a ISO original.';return;}
  message.textContent='Enviando ISO para o armazenamento privado do Codespace...';
  try{
    const result=await api('/api/iso',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-File-Name':encodeURIComponent(file.name)},body:file});
    message.textContent='ISO recebida · '+result.bytes+' bytes · SHA-256 '+result.sha256;
  }catch(error){message.textContent='Falha: '+error.message;}
};
document.getElementById('build').onclick=async()=>{
  message.textContent='Iniciando compilação privada...';
  try{const result=await api('/api/build',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});message.textContent='Build iniciado: '+result.mode;refresh();}
  catch(error){message.textContent='Falha: '+error.message;}
};
async function refresh(){
  try{
    const state=await api('/api/status');
    message.textContent='Estado: '+state.status+(state.mode?' · '+state.mode:'')+(state.apk_bytes?' · APK '+state.apk_bytes+' bytes':'');
    log.textContent=state.log_tail||'Sem log disponível.';
    log.scrollTop=log.scrollHeight;
  }catch(error){message.textContent='Falha: '+error.message;}
}
document.getElementById('refresh').onclick=refresh;
document.getElementById('download').onclick=async()=>{
  message.textContent='Preparando download do APK...';
  try{
    const response=await fetch('/api/apk',{headers:auth()});
    if(!response.ok){const body=await response.json();throw new Error(body.message||'APK indisponível');}
    const blob=await response.blob();
    const url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download='gow-android-arm64.apk';link.click();
    URL.revokeObjectURL(url);message.textContent='Download do APK iniciado.';
  }catch(error){message.textContent='Falha: '+error.message;}
};
</script></body></html>
"""


def make_handler(state: BuilderState) -> type[http.server.BaseHTTPRequestHandler]:
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "GowPhoneBuilder/1"

        def log_message(self, format_string: str, *args: object) -> None:
            # Avoid logging Authorization headers or uploaded data.
            print(f"[phone-builder] {self.address_string()} {format_string % args}", flush=True)

        def send_bytes(self, status: int, payload: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            if self.close_connection:
                self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)

        def send_json(self, status: int, payload: dict[str, Any]) -> None:
            self.send_bytes(
                status,
                json.dumps(payload, ensure_ascii=False).encode("utf-8"),
                "application/json; charset=utf-8",
            )

        def authorized(self) -> bool:
            value = self.headers.get("Authorization", "")
            prefix = "Bearer "
            if not value.startswith(prefix):
                return False
            return secrets.compare_digest(value[len(prefix):].strip(), state.token)

        def require_auth(self) -> bool:
            if self.authorized():
                return True
            # A rejected request body has not been consumed. Close this HTTP/1.1
            # connection after the 401 so those bytes cannot be parsed as the
            # next request on the same keep-alive socket.
            self.close_connection = True
            self.send_json(401, {"message": "Token inválido ou ausente."})
            return False

        def read_json(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_JSON_BYTES:
                raise ValueError("JSON request length is invalid.")
            value = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(value, dict):
                raise ValueError("JSON body must be an object.")
            return value

        def do_GET(self) -> None:
            if self.path == "/":
                self.send_bytes(200, HTML.encode("utf-8"), "text/html; charset=utf-8")
                return
            if self.path == "/api/status":
                if not self.require_auth():
                    return
                self.send_json(200, state.status())
                return
            if self.path == "/api/apk":
                if not self.require_auth():
                    return
                try:
                    apk = state.apk_path()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/vnd.android.package-archive")
                    self.send_header("Content-Length", str(apk.stat().st_size))
                    self.send_header("Content-Disposition", 'attachment; filename="gow-android-arm64.apk"')
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    with apk.open("rb") as stream:
                        while True:
                            chunk = stream.read(1024 * 1024)
                            if not chunk:
                                break
                            self.wfile.write(chunk)
                except (OSError, RuntimeError) as error:
                    self.send_json(409, {"message": str(error)})
                return
            self.send_json(404, {"message": "Endpoint não encontrado."})

        def do_POST(self) -> None:
            if not self.require_auth():
                return
            try:
                if self.path == "/api/iso":
                    result = state.upload(
                        kind="iso",
                        body=self.rfile,
                        content_length=int(self.headers.get("Content-Length", "0")),
                    )
                    self.send_json(201, result)
                    return
                if self.path == "/api/captures":
                    result = state.upload(
                        kind="captures",
                        body=self.rfile,
                        content_length=int(self.headers.get("Content-Length", "0")),
                    )
                    self.send_json(201, result)
                    return
                if self.path == "/api/build":
                    self.read_json()
                    result = state.start_build()
                    self.send_json(202, result)
                    return
                self.send_json(404, {"message": "Endpoint não encontrado."})
            except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
                self.send_json(400, {"message": str(error)})

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path.home() / ".local/share/gow-phone-builder",
        help="private Codespaces workspace outside the public repository",
    )
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("Port must be between 1024 and 65535.")

    state = BuilderState(args.repo_root, args.work_dir)
    print("")
    print("FORGE phone-only Codespaces builder")
    print(f"Private workspace: {state.work_dir}")
    print(f"Bridge token: {state.token}")
    codespace = os.environ.get("CODESPACE_NAME")
    domain = os.environ.get("GITHUB_CODESPACES_PORT_FORWARDING_DOMAIN", "app.github.dev")
    if codespace:
        print(f"Phone browser URL: https://{codespace}-{args.port}.{domain}")
    else:
        print(f"Open forwarded port {args.port} in the Codespaces Ports panel.")
    print("Keep this token private. The forwarded port is public; every data/build endpoint requires the token.")
    print("No ISO, capture ZIP, ELF, IRX or generated game C++ is written to Git.")
    print("")

    server = http.server.ThreadingHTTPServer((args.host, args.port), make_handler(state))
    server.daemon_threads = True
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        print("\nStopping phone builder server.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

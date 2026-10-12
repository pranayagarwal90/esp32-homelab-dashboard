"""Deployment: /health, clean SIGTERM shutdown, releases picked up without a
restart, and static checks of Dockerfile / .dockerignore / compose.yaml.
Never starts Docker, never reads the developer's .env, never uses port 8090."""
import ast
import io
import json
import os
from pathlib import Path
import re
import runpy
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from contextlib import redirect_stdout
from http.server import ThreadingHTTPServer
from unittest.mock import patch
from urllib.request import urlopen

from firmware_updates import ReleaseStore

BACKEND = Path(__file__).resolve().parent
ROOT = BACKEND.parent

with patch.dict(os.environ, {"DASHBOARD_ENV_FILE": ""}), patch("http.server.HTTPServer"), \
        redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(BACKEND / "server.py"))
globals_ = backend["Handler"].do_GET.__globals__


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class HealthTests(unittest.TestCase):
    def test_health_is_cheap_and_static(self):
        def forbidden(*args, **kwargs):
            raise AssertionError("/health must not do real work")

        stubs = {name: forbidden for name in ("get_status", "docker_status", "get_windows_metrics",
                                              "get_weather", "service_status")}
        server = ThreadingHTTPServer(("127.0.0.1", 0), backend["Handler"])
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        with patch.dict(globals_, stubs):
            thread.start()
            try:
                with urlopen(f"http://127.0.0.1:{server.server_address[1]}/health", timeout=5) as response:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.headers["Content-Type"], "application/json")
                    self.assertEqual(json.loads(response.read()), {"status": "ok"})
            finally:
                server.shutdown()
                server.server_close()


class ShutdownTests(unittest.TestCase):
    def test_sigterm_exits_cleanly(self):
        """What docker stop and systemctl stop send: a clean exit, no SIGKILL."""
        port = free_port()
        with tempfile.TemporaryDirectory() as directory:
            environ = {**os.environ, "DASHBOARD_ENV_FILE": "", "DASHBOARD_BIND": "127.0.0.1",
                       "DASHBOARD_PORT": str(port), "DASHBOARD_PHOTOS_DIR": directory,
                       "DOCKER_STATUS_ENABLED": "false", "PYTHONUNBUFFERED": "1"}
            process = subprocess.Popen([sys.executable, str(BACKEND / "server.py")], env=environ, cwd=directory,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                deadline = time.time() + 15
                while True:
                    try:
                        with urlopen(f"http://127.0.0.1:{port}/health", timeout=1) as response:
                            self.assertEqual(response.status, 200)
                        break
                    except OSError:
                        self.assertIsNone(process.poll(), "server exited early")
                        self.assertLess(time.time(), deadline, "server did not start")
                        time.sleep(0.1)
                process.send_signal(signal.SIGTERM)
                self.assertEqual(process.wait(timeout=5), 0)
                output = process.stdout.read()
                self.assertIn("Stopped", output)
                self.assertNotIn("Traceback", output)
            finally:
                if process.poll() is None:
                    process.kill()
                process.stdout.close()


class ReleaseReloadTests(unittest.TestCase):
    def test_new_release_needs_no_restart(self):
        """A release published into the (mounted) directory is served at once."""
        import base64
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            store = ReleaseStore(True, folder, folder / "manifest.json")
            self.assertEqual(store.check("homedeck-mini", "1.0.0"), (200, {"available": False}))
            for version in ("1.1.0", "1.2.0"):
                image = f"firmware {version}".encode()
                (folder / f"homedeck-mini-{version}.bin").write_bytes(image)
                (folder / "manifest.json").write_text(json.dumps({
                    "manifest_version": 2, "product": "homedeck-mini", "version": version,
                    "filename": f"homedeck-mini-{version}.bin", "size": len(image),
                    "sha256": hashlib.sha256(image).hexdigest(), "notes": "",
                    "key_id": "homedeck-release-2026", "signature": base64.b64encode(bytes(64)).decode()}))
                status, offer = store.check("homedeck-mini", "1.0.0")
                self.assertEqual((status, offer["version"]), (200, version))
                self.assertIsNotNone(store.download(f"homedeck-mini-{version}.bin"))
            self.assertIsNone(store.download("homedeck-mini-1.1.0.bin"))  # Only the current release.


def local_modules(entry):
    """Backend modules server.py needs (transitively)."""
    seen, todo = set(), [entry]
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.add(name)
        tree = ast.parse((BACKEND / f"{name}.py").read_text())
        for node in ast.walk(tree):
            names = [a.name for a in node.names] if isinstance(node, ast.Import) else \
                [node.module] if isinstance(node, ast.ImportFrom) and node.module else []
            todo += [n for n in names if (BACKEND / f"{n}.py").is_file()]
    return {f"backend/{name}.py" for name in seen}


PERSONAL = re.compile(r"192\.168\.|10\.10\.|\b100\.\d+\.|/home/|deepika|HomeServer|HOMESERVER", re.IGNORECASE)


class DockerFileTests(unittest.TestCase):
    def setUp(self):
        self.dockerfile = (ROOT / "Dockerfile").read_text()
        self.ignore = (ROOT / ".dockerignore").read_text()
        self.compose = (ROOT / "compose.yaml").read_text()

    def lines(self, text):
        text = re.sub(r"\\\n\s*", " ", text)  # Join continuation lines.
        return [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith("#")]

    def test_image_contains_only_the_server(self):
        modules = local_modules("server")
        allowed = {line[1:] for line in self.lines(self.ignore) if line.startswith("!")}
        self.assertEqual(self.lines(self.ignore)[0], "*")  # Allowlist.
        self.assertEqual(allowed, modules)
        copies = [line for line in self.lines(self.dockerfile) if line.startswith("COPY")]
        self.assertEqual(len(copies), 1)
        self.assertEqual(set(re.findall(r"backend/\w+\.py", copies[0])), modules)
        for denied in (".env", "private/", "**/*.pem", "**/*.key", "firmware-releases/", ".git/"):
            self.assertIn(denied, self.lines(self.ignore))

    def test_dockerfile_hygiene(self):
        lines = self.lines(self.dockerfile)
        self.assertTrue(lines[0].startswith("FROM python:3.12-slim"))
        self.assertFalse([l for l in lines if l.startswith(("ARG", "ADD"))])  # No build-time secrets.
        self.assertIn("USER homedeck", lines)
        self.assertLess(lines.index("USER homedeck"), next(i for i, l in enumerate(lines) if l.startswith("CMD")))
        self.assertIn('CMD ["python", "backend/server.py"]', lines)  # Exec form: PID 1 gets SIGTERM.
        self.assertIn("EXPOSE 8090", lines)
        self.assertTrue(any("HEALTHCHECK" in l for l in lines) and "/health" in self.dockerfile)
        self.assertIn('DASHBOARD_ENV_FILE=""', self.dockerfile)  # Environment only, never a baked .env.
        self.assertNotRegex(self.dockerfile, r"pip install|apt-get|curl")

    def test_compose(self):
        c = self.compose
        self.assertIn("homedeck-backend:", c)
        self.assertIn("restart: unless-stopped", c)
        self.assertIn('"${HOMEDECK_PORT:-8090}:8090"', c)
        self.assertIn("path: .env", c)
        self.assertIn("./firmware-releases:/data/firmware-releases:ro", c)
        self.assertIn("./photos-ready:/data/photos:ro", c)
        for forbidden in ("privileged", "docker.sock", "network_mode", "pid:", "ipc:", "/private", ".pem", ".key"):
            self.assertNotIn(forbidden, c)
        self.assertNotRegex(c, r"(?m)^\s+- \S*host-gateway")  # Only as a commented option.
        for path in ("FIRMWARE_RELEASE_DIR: /data/firmware-releases",
                     "FIRMWARE_MANIFEST_FILE: /data/firmware-releases/manifest.json",
                     "DASHBOARD_PHOTOS_DIR: /data/photos", "DASHBOARD_BIND: 0.0.0.0"):
            self.assertIn(path, c)

    def test_no_personal_data_in_deployment_files(self):
        for name in ("Dockerfile", ".dockerignore", "compose.yaml"):
            self.assertIsNone(PERSONAL.search((ROOT / name).read_text()), name)

    def test_mounted_folders_exist_in_a_clone(self):
        # Tracked placeholders: Docker would otherwise create them owned by root.
        for name in ("firmware-releases", "photos-ready"):
            self.assertTrue((ROOT / name / ".gitkeep").is_file())
            self.assertIn(f"!{name}/.gitkeep", (ROOT / ".gitignore").read_text())


if __name__ == "__main__":
    unittest.main()

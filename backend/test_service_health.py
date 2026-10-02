"""Isolated service checks; never contact production or start the real API."""
import io
import json
import os
from pathlib import Path
import runpy
import socket
import threading
import time
import unittest
from contextlib import redirect_stdout
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch
from urllib.request import urlopen


with patch("http.server.HTTPServer"), redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
service_status = backend["service_status"]
check_http = backend["check_service_http"]
globals_ = service_status.__globals__
LEGACY_KEYS = {"jellyfin", "navidrome", "metube", "bazarr", "ollama", "cloudflare", "mcp"}
NEW_KEYS = {"nextcloud", "immich", "technical_blog"}
KEYS = LEGACY_KEYS | NEW_KEYS


class HealthHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/slow":
            time.sleep(1)
        code = {"/ok": 204, "/fail": 503, "/auth": 401, "/redirect": 302}.get(self.path, 200)
        self.send_response(code)
        self.end_headers()
        if self.path == "/jellyfin":
            self.wfile.write(b"Healthy\n")
        elif self.path == "/unhealthy":
            self.wfile.write(b"Unhealthy")
        elif self.path == "/oversized":
            self.wfile.write(b"Healthy" + b" " * 4096)
        bodies = {
            "/nextcloud": {"installed": True, "maintenance": False, "needsDbUpgrade": False},
            "/maintenance": {"installed": True, "maintenance": True, "needsDbUpgrade": False},
            "/upgrade": {"installed": True, "maintenance": False, "needsDbUpgrade": True},
            "/immich": {"res": "pong"},
            "/ollama": {"version": "test-version"},
            "/wrong": {},
        }
        if self.path in bodies:
            self.wfile.write(json.dumps(bodies[self.path]).encode())

    def log_message(self, *args):
        pass


class ServiceHealthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), HealthHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        self.env = patch.dict(os.environ, {}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        defaults = patch.dict(globals_, {"DEFAULT_SERVICE_HEALTH_URLS": {}})
        defaults.start()
        self.addCleanup(defaults.stop)

    def test_http_results_and_invalid_urls(self):
        for path, expected in [("/ok", True), ("/fail", False), ("/auth", False), ("/redirect", False)]:
            with self.subTest(path=path):
                self.assertIs(check_http(self.url + path), expected)
        for url in ["invalid", "ftp://localhost/", "http://localhost:invalid", "http://user:pass@localhost/", self.url + "/#fragment"]:
            with self.subTest(url=url):
                self.assertIs(check_http(url), False)

    def test_connection_failure(self):
        with patch.object(globals_["HTTPConnection"], "request", side_effect=ConnectionRefusedError):
            self.assertIs(check_http(self.url), False)

    def test_timeout_and_parallel_checks(self):
        for name in KEYS:
            os.environ[f"SERVICE_{name.upper()}_HEALTH_URL"] = self.url + "/slow"
        start = time.monotonic()
        result = service_status({})
        elapsed = time.monotonic() - start
        self.assertEqual(result, dict.fromkeys(KEYS, False))
        self.assertLess(elapsed, 1.5, f"Checks were not concurrent: {elapsed:.3f}s")
        print(f"Ten slow services completed in {elapsed:.3f}s")

    def test_fallback_and_application_override(self):
        docker = {"containers": [
            {"name": name, "running": name != "metube"} for name in KEYS
        ]}
        result = service_status(docker)
        self.assertEqual(result, {name: name in LEGACY_KEYS and name != "metube" for name in KEYS})
        os.environ["SERVICE_JELLYFIN_HEALTH_URL"] = self.url + "/jellyfin"
        self.assertIs(service_status({})["jellyfin"], True)
        os.environ["SERVICE_BAZARR_HEALTH_URL"] = self.url + "/fail"
        self.assertIs(service_status(docker)["bazarr"], False)

    def test_new_services_require_configured_urls(self):
        for name in NEW_KEYS:
            path = "/" + name if name != "technical_blog" else "/ok"
            os.environ[f"SERVICE_{name.upper()}_HEALTH_URL"] = self.url + path
        result = service_status({})
        self.assertTrue(all(result[name] is True for name in NEW_KEYS))

    def test_unexpected_check_failure_is_isolated(self):
        os.environ["SERVICE_MCP_HEALTH_URL"] = self.url
        with patch.dict(globals_, {"check_service_http": lambda *args: 1 / 0}):
            result = service_status({"containers": [{"name": "navidrome", "running": True}]})
        self.assertIs(result["mcp"], False)
        self.assertIs(result["navidrome"], True)

    def test_json_health_validation(self):
        for service in {"nextcloud", "immich", "ollama"}:
            self.assertIs(check_http(self.url + "/" + service, service), True)
            self.assertIs(check_http(self.url + "/wrong", service), False)
            self.assertIs(check_http(self.url + "/ok", service), False)
        self.assertIs(check_http(self.url + "/maintenance", "nextcloud"), False)
        self.assertIs(check_http(self.url + "/upgrade", "nextcloud"), False)

    def test_jellyfin_requires_healthy_body(self):
        self.assertIs(check_http(self.url + "/jellyfin", "jellyfin"), True)
        for path in ["/ok", "/unhealthy", "/oversized", "/fail"]:
            with self.subTest(path=path):
                self.assertIs(check_http(self.url + path, "jellyfin"), False)

    def test_verified_application_failure_never_uses_running_container(self):
        docker = {"containers": [{"name": name, "running": True}
                                 for name in ["jellyfin", "navidrome", "metube", "ollama"]]}
        for name in ["jellyfin", "navidrome", "metube", "ollama"]:
            os.environ[f"SERVICE_{name.upper()}_HEALTH_URL"] = self.url + "/fail"
        result = service_status(docker)
        self.assertTrue(all(result[name] is False for name in ["jellyfin", "navidrome", "metube", "ollama"]))

    def test_metube_default_and_explicit_disable(self):
        with patch.dict(globals_, {"DEFAULT_SERVICE_HEALTH_URLS": {"metube": self.url + "/"}}):
            self.assertIs(service_status({})["metube"], True)
            os.environ["SERVICE_METUBE_HEALTH_URL"] = ""
            self.assertIs(service_status({})["metube"], False)
            self.assertIs(service_status({"containers": [{"name": "metube", "running": True}]})["metube"], True)

    def test_verified_defaults_and_environment_override(self):
        with patch.dict(globals_, {"DEFAULT_SERVICE_HEALTH_URLS": {"immich": self.url + "/immich"}}):
            self.assertIs(service_status({})["immich"], True)
            os.environ["SERVICE_IMMICH_HEALTH_URL"] = self.url + "/fail"
            self.assertIs(service_status({})["immich"], False)
            os.environ["SERVICE_IMMICH_HEALTH_URL"] = ""
            self.assertIs(service_status({})["immich"], False)

    def test_status_route_preserves_boolean_schema(self):
        os.environ["SERVICE_JELLYFIN_HEALTH_URL"] = self.url + "/jellyfin"
        os.environ["SERVICE_BAZARR_HEALTH_URL"] = self.url + "/fail"
        replacements = {
            "get_windows_metrics": lambda: {"available": True, "hostname": "test-host"},
            "docker_status": lambda: {"containers": [], "available": True, "running": 0},
            "get_weather": lambda: {"available": True},
        }
        with patch.dict(globals_, replacements):
            server = ThreadingHTTPServer(("127.0.0.1", 0), backend["Handler"])
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with urlopen(f"http://127.0.0.1:{server.server_port}/api/status", timeout=2) as response:
                    self.assertEqual(response.status, 200)
                    payload = json.load(response)
            finally:
                server.shutdown()
                server.server_close()
                thread.join()
        services = payload["services"]
        self.assertEqual(set(services), KEYS)
        self.assertTrue(all(type(value) is bool for value in services.values()))
        self.assertIs(services["jellyfin"], True)
        self.assertIs(services["bazarr"], False)
        self.assertEqual(payload["hostname"], "test-host")
        self.assertIn("timezones", payload)
        print("Sample services: " + json.dumps(services, sort_keys=True))

    def test_status_route_stays_fast_with_unavailable_and_slow_services(self):
        # A bound, non-listening socket provides a real refused port without
        # guessing an unused production port or touching a production service.
        with socket.socket() as unavailable:
            unavailable.bind(("127.0.0.1", 0))
            os.environ["SERVICE_METUBE_HEALTH_URL"] = f"http://127.0.0.1:{unavailable.getsockname()[1]}/"
            os.environ["SERVICE_JELLYFIN_HEALTH_URL"] = self.url + "/jellyfin"
            os.environ["SERVICE_NAVIDROME_HEALTH_URL"] = self.url + "/slow"
            os.environ["SERVICE_OLLAMA_HEALTH_URL"] = self.url + "/ollama"
            replacements = {
                "get_windows_metrics": lambda: {"available": True, "hostname": "test-host"},
                "docker_status": lambda: {"containers": [{"name": "metube", "running": True}]},
                "get_weather": lambda: {"available": True},
            }
            with patch.dict(globals_, replacements):
                server = ThreadingHTTPServer(("127.0.0.1", 0), backend["Handler"])
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                try:
                    start = time.monotonic()
                    with urlopen(f"http://127.0.0.1:{server.server_port}/api/status", timeout=2) as response:
                        self.assertEqual(response.status, 200)
                        payload = json.load(response)
                    elapsed = time.monotonic() - start
                finally:
                    server.shutdown()
                    server.server_close()
                    thread.join()
        services = payload["services"]
        self.assertEqual(set(services), KEYS)
        self.assertTrue(all(type(value) is bool for value in services.values()))
        self.assertIs(services["jellyfin"], True)
        self.assertIs(services["ollama"], True)
        self.assertIs(services["metube"], False)
        self.assertIs(services["navidrome"], False)
        self.assertLess(elapsed, 1.5)
        print(f"/api/status with refused and slow services: HTTP 200 in {elapsed:.3f}s")


if __name__ == "__main__":
    unittest.main()

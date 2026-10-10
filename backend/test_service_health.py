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

import config

# Hermetic: never read the developer's .env.
with patch.dict(os.environ, {"DASHBOARD_ENV_FILE": ""}), patch("http.server.HTTPServer"), \
        redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
check_http = backend["check_service_http"]
globals_ = backend["service_status"].__globals__


def test_config():
    """Config from the test's os.environ only (setUp clears it)."""
    return config.load(environ=dict(os.environ), env_file="")


def service_status(docker):
    with patch.dict(globals_, {"CONFIG": test_config()}):
        return backend["service_status"](docker)


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

    def enable(self, *ids, **urls):
        """SERVICES=ids plus SERVICE_<ID>_HEALTH_URL=self.url + path."""
        os.environ["SERVICES"] = ",".join(ids)
        for service_id, path in urls.items():
            os.environ[f"SERVICE_{service_id.upper()}_HEALTH_URL"] = self.url + path

    # --- The Phase 2 semantics -----------------------------------------------------

    def test_not_enabled_service_is_null(self):
        os.environ["SERVICE_JELLYFIN_HEALTH_URL"] = self.url + "/jellyfin"  # URL alone does not enable.
        running = {"containers": [{"name": name, "running": True} for name in KEYS]}
        with patch.dict(globals_, {"check_service_http": lambda *args: self.fail("checked")}):
            self.assertEqual(service_status(running), dict.fromkeys(KEYS))

    def test_enabled_with_healthy_url_is_true(self):
        self.enable("jellyfin", jellyfin="/jellyfin")
        self.assertIs(service_status({})["jellyfin"], True)

    def test_enabled_with_failing_url_is_false(self):
        self.enable("immich", immich="/fail")
        self.assertIs(service_status({})["immich"], False)

    def test_enabled_without_url_uses_running_container(self):
        self.enable("metube", "cloudflare")
        docker = {"containers": [{"name": "metube", "running": True},
                                 {"name": "cloudflared-tunnel", "running": True}]}
        result = service_status(docker)
        self.assertEqual((result["metube"], result["cloudflare"]), (True, True))

    def test_enabled_without_url_or_container_is_false(self):
        self.enable("jellyfin", "nextcloud")
        stopped = {"containers": [{"name": "jellyfin", "running": False}]}
        result = service_status(stopped)
        # Configured but no healthy target: down. Nextcloud has no Docker identity.
        self.assertEqual((result["jellyfin"], result["nextcloud"]), (False, False))

    def test_not_enabled_service_ignores_running_container(self):
        self.enable("ollama")
        running = {"containers": [{"name": name, "running": True} for name in KEYS]}
        result = service_status(running)
        self.assertIs(result["ollama"], True)
        self.assertEqual({k: v for k, v in result.items() if k != "ollama"}, dict.fromkeys(KEYS - {"ollama"}))

    def test_docker_disabled_never_runs_docker(self):
        os.environ["DOCKER_STATUS_ENABLED"] = "false"
        self.enable("jellyfin", "metube", jellyfin="/jellyfin")
        with patch.dict(globals_, {"CONFIG": test_config()}), \
                patch.object(globals_["subprocess"], "run", side_effect=AssertionError("docker called")):
            docker = backend["docker_status"]()
            result = backend["service_status"](docker)
        self.assertEqual((docker["available"], docker["containers"]), (False, []))
        self.assertIs(result["jellyfin"], True)   # URL checks still run.
        self.assertIs(result["metube"], False)    # Enabled, no URL, no Docker: down.
        self.assertIsNone(result["navidrome"])    # Not enabled: unknown.

    def test_generic_config_has_no_false_services(self):
        with patch.dict(globals_, {"check_service_http": lambda *args: self.fail("probed")}):
            for docker in ({}, {"containers": []}, {"containers": [{"name": "jellyfin", "running": True}]}):
                with self.subTest(docker=docker):
                    self.assertEqual(service_status(docker), dict.fromkeys(KEYS))

    def test_docker_enabled_runs_docker_cli_once(self):
        completed = backend["subprocess"].CompletedProcess([], 0, "metube|Up 2 hours\n", "")
        with patch.dict(globals_, {"CONFIG": test_config()}), \
                patch.object(globals_["subprocess"], "run", return_value=completed) as run:
            docker = backend["docker_status"]()
        run.assert_called_once()
        self.assertEqual(docker["containers"], [{"name": "metube", "status": "Up 2 hours", "running": True}])

    # --- Existing check behaviour, with the services enabled -------------------------

    def test_timeout_and_parallel_checks(self):
        self.enable(*KEYS, **{name: "/slow" for name in KEYS})
        start = time.monotonic()
        result = service_status({})
        elapsed = time.monotonic() - start
        self.assertEqual(result, dict.fromkeys(KEYS, False))
        self.assertLess(elapsed, 1.5, f"Checks were not concurrent: {elapsed:.3f}s")
        print(f"Ten slow services completed in {elapsed:.3f}s")

    def test_url_check_wins_over_running_container(self):
        self.enable("jellyfin", "bazarr", jellyfin="/jellyfin", bazarr="/fail")
        docker = {"containers": [{"name": name, "running": True} for name in KEYS]}
        result = service_status(docker)
        self.assertEqual((result["jellyfin"], result["bazarr"]), (True, False))

    def test_new_services_require_configured_urls(self):
        self.enable(*NEW_KEYS, **{name: "/" + name if name != "technical_blog" else "/ok" for name in NEW_KEYS})
        result = service_status({})
        self.assertTrue(all(result[name] is True for name in NEW_KEYS))

    def test_unexpected_check_failure_is_isolated(self):
        self.enable("mcp", "navidrome", mcp="")
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
        names = ["jellyfin", "navidrome", "metube", "ollama"]
        self.enable(*names, **dict.fromkeys(names, "/fail"))
        docker = {"containers": [{"name": name, "running": True} for name in names]}
        result = service_status(docker)
        self.assertTrue(all(result[name] is False for name in names))

    def test_blank_url_falls_back_to_docker(self):
        self.enable("metube", metube="/")
        self.assertIs(service_status({})["metube"], True)
        os.environ["SERVICE_METUBE_HEALTH_URL"] = ""
        self.assertIs(service_status({})["metube"], False)
        self.assertIs(service_status({"containers": [{"name": "metube", "running": True}]})["metube"], True)

    # --- /api/status ---------------------------------------------------------------

    def get_status(self, docker):
        replacements = {
            "CONFIG": test_config(),
            "get_windows_metrics": lambda: {"available": True, "hostname": "test-host"},
            "docker_status": lambda: docker,
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
                return payload, time.monotonic() - start
            finally:
                server.shutdown()
                server.server_close()
                thread.join()

    def test_status_route_preserves_service_keys(self):
        self.enable("jellyfin", "bazarr", jellyfin="/jellyfin", bazarr="/fail")
        payload, _ = self.get_status({"containers": [], "available": True, "running": 0})
        services = payload["services"]
        self.assertEqual(list(services), list(backend["SERVICE_IDS"]))  # Every key, same order.
        self.assertEqual(services, dict.fromkeys(KEYS) | {"jellyfin": True, "bazarr": False})
        self.assertEqual(payload["hostname"], "test-host")
        self.assertIn("timezones", payload)
        print("Sample services: " + json.dumps(services, sort_keys=True))

    def test_status_route_stays_fast_with_unavailable_and_slow_services(self):
        # A bound, non-listening socket provides a real refused port without
        # guessing an unused production port or touching a production service.
        with socket.socket() as unavailable:
            unavailable.bind(("127.0.0.1", 0))
            self.enable("metube", "jellyfin", "navidrome", "ollama",
                        jellyfin="/jellyfin", navidrome="/slow", ollama="/ollama")
            os.environ["SERVICE_METUBE_HEALTH_URL"] = f"http://127.0.0.1:{unavailable.getsockname()[1]}/"
            payload, elapsed = self.get_status({"containers": [{"name": "metube", "running": True}]})
        services = payload["services"]
        self.assertEqual(set(services), KEYS)
        self.assertEqual({k: services[k] for k in ("jellyfin", "ollama", "metube", "navidrome")},
                         {"jellyfin": True, "ollama": True, "metube": False, "navidrome": False})
        self.assertIsNone(services["immich"])
        self.assertLess(elapsed, 1.5)
        print(f"/api/status with refused and slow services: HTTP 200 in {elapsed:.3f}s")


if __name__ == "__main__":
    unittest.main()

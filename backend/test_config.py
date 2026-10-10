"""Configuration tests: temporary files and fake environments only; never
reads the developer's .env or contacts a service."""
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import threading
import unittest
from contextlib import redirect_stdout
from http.server import ThreadingHTTPServer
from unittest.mock import Mock, patch
from urllib.request import urlopen

import config
from config import ConfigError, Source

# Hermetic: never read the developer's .env.
with patch.dict(os.environ, {"DASHBOARD_ENV_FILE": ""}), patch("http.server.HTTPServer"), \
        redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
globals_ = backend["get_status"].__globals__

STATUS_KEYS = {"hostname", "host_available", "uptime_hours", "cpu", "memory", "disks", "gpu", "wifi",
               "ethernet", "docker", "services", "timezones", "weather", "timestamp"}


def load(environ=None, text=None):
    """Config from a fake environment and, optionally, .env text."""
    if text is None:
        return config.load(environ=environ or {}, env_file="")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / ".env"
        path.write_text(text)
        return config.load(environ=environ or {}, env_file=str(path))


class EnvFileTests(unittest.TestCase):
    def test_missing_env_file_is_normal(self):
        cfg = config.load(environ={}, env_file="/nonexistent/dashboard.env")
        self.assertIsNone(cfg.env_file)
        self.assertEqual((cfg.bind, cfg.port, cfg.timezone, cfg.host_label),
                         ("0.0.0.0", 8090, "UTC", "Server"))

    def test_env_file_values_are_used(self):
        cfg = load(text="DASHBOARD_PORT=9000\nDASHBOARD_TIMEZONE=Europe/Paris\n")
        self.assertEqual((cfg.port, cfg.timezone), (9000, "Europe/Paris"))

    def test_process_environment_overrides_env_file(self):
        cfg = load({"DASHBOARD_PORT": "9100", "HOST_METRICS_URL": ""},
                   "DASHBOARD_PORT=9000\nHOST_METRICS_URL=http://192.0.2.10:9183/\n")
        self.assertEqual(cfg.port, 9100)
        self.assertEqual(cfg.host_metrics_url, "")  # A blank environment value still wins.

    def test_default_location_and_dashboard_env_file_variable(self):
        self.assertEqual(config.DEFAULT_ENV_FILE, config.PROJECT_ROOT / ".env")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "custom.env"
            path.write_text("DASHBOARD_HOST_LABEL=custom\n")
            self.assertEqual(config.load(environ={"DASHBOARD_ENV_FILE": str(path)}).host_label, "custom")
            self.assertEqual(config.load(environ={"DASHBOARD_ENV_FILE": ""}).host_label, "Server")

    def test_comments_blank_lines_and_quotes(self):
        values = config.parse_env_text(
            "# comment\n\n  KEY_A = value  \nKEY_B=\"quoted # not a comment\"\n"
            "KEY_C='single'\nKEY_D=value # trailing comment\nKEY_E=a#b\nKEY_F=\nKEY_G=x=y\n")
        self.assertEqual(values, {"KEY_A": "value", "KEY_B": "quoted # not a comment", "KEY_C": "single",
                                  "KEY_D": "value", "KEY_E": "a#b", "KEY_F": "", "KEY_G": "x=y"})

    def test_values_are_never_expanded(self):
        values = config.parse_env_text("A=$HOME\nB=`id`\nC=$(id)\n")
        self.assertEqual(values, {"A": "$HOME", "B": "`id`", "C": "$(id)"})

    def test_malformed_lines_are_rejected(self):
        for text in ("JUST_A_KEY\n", "BAD KEY=1\n", "1KEY=1\n", "=value\n", "export A=1\n", "A=\"open\n"):
            with self.subTest(text=text), self.assertRaises(ConfigError) as caught:
                config.parse_env_text(text, "test.env")
            self.assertIn("test.env:1", str(caught.exception))


class TypedValueTests(unittest.TestCase):
    def source(self, **values):
        return Source(values, {})

    def test_booleans(self):
        for text, expected in [("true", True), ("1", True), ("YES", True), ("on", True),
                               ("false", False), ("0", False), ("No", False), ("off", False)]:
            with self.subTest(text=text):
                self.assertIs(config.get_bool(self.source(FLAG=text), "FLAG", None), expected)
        self.assertIs(config.get_bool(self.source(FLAG=""), "FLAG", True), True)
        self.assertIs(config.get_bool(self.source(), "FLAG", False), False)
        with self.assertRaises(ConfigError):
            config.get_bool(self.source(FLAG="maybe"), "FLAG", True)

    def test_ints(self):
        self.assertEqual(config.get_int(self.source(N=" 42 "), "N", 1), 42)
        self.assertEqual(config.get_int(self.source(N=""), "N", 7), 7)
        for text in ("4.2", "abc", "0x10"):
            with self.subTest(text=text), self.assertRaises(ConfigError):
                config.get_int(self.source(N=text), "N", 1)

    def test_floats(self):
        self.assertEqual(config.get_float(self.source(F="-1.5"), "F", 0.0), -1.5)
        self.assertEqual(config.get_float(self.source(), "F", 2.5), 2.5)
        self.assertIsNone(config.get_optional_float(self.source(F=" "), "F"))
        for text in ("nan", "inf", "-inf", "1,5", "x"):
            with self.subTest(text=text), self.assertRaises(ConfigError):
                config.get_optional_float(self.source(F=text), "F")
        with self.assertRaises(ConfigError):
            config.get_optional_float(self.source(F="10"), "F", 0, 5)

    def test_port_range(self):
        self.assertEqual(load({"DASHBOARD_PORT": "1"}).port, 1)
        self.assertEqual(load({"DASHBOARD_PORT": "65535"}).port, 65535)
        for text in ("0", "65536", "-1", "http", "80 80"):
            with self.subTest(text=text), self.assertRaises(ConfigError):
                load({"DASHBOARD_PORT": text})

    def test_bind(self):
        self.assertEqual(load({"DASHBOARD_BIND": "127.0.0.1"}).bind, "127.0.0.1")
        with self.assertRaises(ConfigError):
            load({"DASHBOARD_BIND": "0.0.0.0 8090"})

    def test_blank_core_settings_use_defaults(self):
        cfg = load({name: "" for name in ("DASHBOARD_BIND", "DASHBOARD_PORT", "DASHBOARD_TIMEZONE",
                                          "DASHBOARD_HOST_LABEL", "DASHBOARD_PHOTOS_DIR",
                                          "DOCKER_STATUS_ENABLED")})
        self.assertEqual((cfg.bind, cfg.port, cfg.timezone, cfg.host_label, cfg.docker_status_enabled),
                         ("0.0.0.0", 8090, "UTC", "Server", True))
        self.assertEqual(cfg.photos_dir, config.PROJECT_ROOT / "photos-ready")


class TimezoneTests(unittest.TestCase):
    def test_default_and_valid_timezone(self):
        self.assertEqual(load().timezone, "UTC")
        self.assertEqual(load({"DASHBOARD_TIMEZONE": "Asia/Tokyo"}).timezone, "Asia/Tokyo")

    def test_invalid_timezone_fails_instead_of_falling_back(self):
        for zone in ("Mars/Olympus", "America", "../etc/passwd", "UTC+5"):
            with self.subTest(zone=zone), self.assertRaises(ConfigError) as caught:
                load({"DASHBOARD_TIMEZONE": zone})
            self.assertIn("DASHBOARD_TIMEZONE", str(caught.exception))

    def test_world_clocks(self):
        self.assertEqual(load().world_clocks, ())
        clocks = load({"DASHBOARD_WORLD_CLOCKS": " London=Europe/London , New York=America/New_York"}).world_clocks
        self.assertEqual([(c.key, c.label, c.zone) for c in clocks],
                         [("london", "London", "Europe/London"), ("new_york", "New York", "America/New_York")])

    def test_invalid_world_clocks_fail(self):
        for text in ("London", "London=", "=Europe/London", "London=Nowhere/City", "Local=UTC",
                     "A=UTC,a=UTC", "London=Europe/London,", "<b>=UTC"):
            with self.subTest(text=text), self.assertRaises(ConfigError):
                load({"DASHBOARD_WORLD_CLOCKS": text})


class IntegrationConfigTests(unittest.TestCase):
    def test_weather(self):
        self.assertEqual(load({"WEATHER_LATITUDE": "51.5", "WEATHER_LONGITUDE": "-0.13"}).weather_location,
                         (51.5, -0.13))
        self.assertEqual(load({"WEATHER_LATITUDE": "-90", "WEATHER_LONGITUDE": "180"}).weather_location,
                         (-90.0, 180.0))
        blank = load({"WEATHER_LATITUDE": "", "WEATHER_LONGITUDE": ""})
        self.assertEqual((blank.weather_location, blank.warnings), (None, ()))

    def test_invalid_latitude_or_longitude_disables_weather_with_warning(self):
        for environ in ({"WEATHER_LATITUDE": "90.1", "WEATHER_LONGITUDE": "0"},
                        {"WEATHER_LATITUDE": "0", "WEATHER_LONGITUDE": "-180.5"},
                        {"WEATHER_LATITUDE": "north", "WEATHER_LONGITUDE": "0"},
                        {"WEATHER_LATITUDE": "51.5"}):
            with self.subTest(environ=environ):
                cfg = load(environ)
                self.assertIsNone(cfg.weather_location)
                self.assertTrue(cfg.warnings and cfg.warnings[0].startswith("weather disabled"))

    def test_host_metrics_url(self):
        self.assertEqual(load().host_metrics_url, "")
        url = "http://192.0.2.10:9183/"
        self.assertEqual(load({"HOST_METRICS_URL": url}).host_metrics_url, url)
        cfg = load({"HOST_METRICS_URL": "192.0.2.10:9183"})
        self.assertEqual(cfg.host_metrics_url, "")
        self.assertIn("host metrics disabled", cfg.warnings[0])

    def test_service_urls(self):
        cfg = load({"SERVICES": "jellyfin,technical_blog,immich",
                    "SERVICE_JELLYFIN_HEALTH_URL": "http://192.0.2.20:8096/health",
                    "SERVICE_TECHNICAL_BLOG_HEALTH_URL": "https://blog.example/",
                    "SERVICE_IMMICH_HEALTH_URL": "", "OTHER": "x"})
        self.assertEqual(cfg.service_urls, {"jellyfin": "http://192.0.2.20:8096/health",
                                            "technical_blog": "https://blog.example/"})
        self.assertEqual(cfg.warnings, ())
        bad = load({"SERVICES": "navidrome", "SERVICE_NAVIDROME_HEALTH_URL": "http://user:pw@192.0.2.20/"})
        self.assertIn("navidrome", bad.service_urls)  # Kept, so it reports down...
        self.assertIn("SERVICE_NAVIDROME_HEALTH_URL", bad.warnings[0])  # ...and is reported.
        self.assertNotIn("pw", bad.warnings[0])

    def test_url_for_unlisted_or_unknown_service_is_reported_not_used(self):
        cfg = load({"SERVICES": "ollama", "SERVICE_JELLYFIN_HEALTH_URL": "http://192.0.2.20:8096/health",
                    "SERVICE_JELIFIN_HEALTH_URL": "http://192.0.2.20/", "SERVICE_NAVIDROME_HEALTH_URL": ""})
        self.assertEqual(cfg.service_urls, {})
        self.assertEqual(len(cfg.warnings), 2)
        self.assertTrue(any("SERVICE_JELIFIN_HEALTH_URL: not a known service" in w for w in cfg.warnings))
        self.assertTrue(any("jellyfin is not in SERVICES" in w for w in cfg.warnings))


class ServicesConfigTests(unittest.TestCase):
    def enabled(self, text):
        return load({"SERVICES": text}).enabled_services

    def test_blank_or_absent_enables_nothing(self):
        self.assertEqual(load().enabled_services, ())
        for text in ("", "   ", ",", " , ,"):
            with self.subTest(text=text):
                self.assertEqual(self.enabled(text), ())

    def test_one_and_multiple_services(self):
        self.assertEqual(self.enabled("jellyfin"), ("jellyfin",))
        self.assertEqual(self.enabled("jellyfin,ollama,immich"), ("jellyfin", "ollama", "immich"))

    def test_whitespace_case_and_duplicates(self):
        self.assertEqual(self.enabled("  Jellyfin ,OLLAMA,, jellyfin ,ollama "), ("jellyfin", "ollama"))
        self.assertEqual(self.enabled("Technical_Blog"), ("technical_blog",))

    def test_order_is_the_registry_order(self):
        expected = ("jellyfin", "metube", "ollama", "immich", "technical_blog")
        self.assertEqual(self.enabled("technical_blog,immich,ollama,metube,jellyfin"), expected)
        self.assertEqual(self.enabled("jellyfin,metube,ollama,immich,technical_blog"), expected)

    def test_every_known_id_is_accepted(self):
        from services import SERVICE_IDS
        self.assertEqual(SERVICE_IDS, ("jellyfin", "navidrome", "metube", "bazarr", "ollama", "cloudflare",
                                       "mcp", "nextcloud", "immich", "technical_blog"))
        self.assertEqual(self.enabled(",".join(reversed(SERVICE_IDS))), SERVICE_IDS)

    def test_unknown_service_fails_startup(self):
        for text in ("jelifin", "jellyfin,plex", "technical-blog", "jellyfin ollama"):
            with self.subTest(text=text), self.assertRaises(ConfigError) as caught:
                self.enabled(text)
            self.assertIn("SERVICES: unknown service", str(caught.exception))
            self.assertIn("known: jellyfin", str(caught.exception))

    def test_generic_defaults_have_no_integrations(self):
        cfg = load()
        self.assertEqual((cfg.weather_location, cfg.host_metrics_url, cfg.service_urls, cfg.world_clocks),
                         (None, "", {}, ()))
        self.assertEqual((cfg.ai.primary_url, cfg.ai.fallback_url), ("", "http://127.0.0.1:11434"))
        self.assertIs(cfg.docker_status_enabled, True)
        self.assertEqual(cfg.warnings, ())


class PhotoPathTests(unittest.TestCase):
    def test_relative_path_is_resolved_from_project_root(self):
        self.assertEqual(load({"DASHBOARD_PHOTOS_DIR": "./photos-ready"}).photos_dir,
                         config.PROJECT_ROOT / "photos-ready")
        self.assertEqual(load({"DASHBOARD_PHOTOS_DIR": "data/../pics"}).photos_dir, config.PROJECT_ROOT / "pics")

    def test_absolute_and_home_paths(self):
        self.assertEqual(load({"DASHBOARD_PHOTOS_DIR": "/srv/photos"}).photos_dir, Path("/srv/photos"))
        with patch.dict(os.environ, {"HOME": "/home/example"}):
            self.assertEqual(load({"DASHBOARD_PHOTOS_DIR": "~/photos"}).photos_dir, Path("/home/example/photos"))


class ServerConfigTests(unittest.TestCase):
    """The server with configs built here; external calls are stubbed."""

    def serve(self, cfg, **replacements):
        state = patch.dict(globals_, {"CONFIG": cfg, "_host_cache": None, "_host_cache_time": 0,
                                     "_weather_cache": None, **replacements})
        state.start()
        self.addCleanup(state.stop)
        server = ThreadingHTTPServer(("127.0.0.1", 0), backend["Handler"])
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return f"http://127.0.0.1:{server.server_port}"

    def get(self, url):
        with urlopen(url, timeout=5) as response:
            return response.status, response.headers.get("Content-Type"), response.read()

    def no_network(self):
        return {"urlopen": Mock(side_effect=AssertionError("network call")),
                "docker_status": lambda: {"available": False, "running": 0, "containers": []}}

    def test_generic_defaults_status(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg = load({"DASHBOARD_PHOTOS_DIR": directory + "/photos"})
            base = self.serve(cfg, **self.no_network())
            code, _, body = self.get(base + "/api/status")
        payload = json.loads(body)
        self.assertEqual(code, 200)
        self.assertEqual(set(payload), STATUS_KEYS)
        self.assertEqual((payload["host_available"], payload["hostname"]), (False, "Server"))
        self.assertEqual(payload["weather"], {"available": False, "error": "weather not configured"})
        self.assertEqual(payload["timezones"]["local"]["timezone"], "UTC")
        # The current firmware reads these names: placeholders, never missing.
        for key in ("india", "singapore", "london"):
            self.assertEqual(payload["timezones"][key]["time"], "--:--")
        # SERVICES blank: every key present, none falsely offline.
        self.assertEqual(payload["services"], dict.fromkeys(backend["SERVICE_IDS"]))

    def test_personal_style_configuration_keeps_legacy_shape(self):
        cfg = load({"DASHBOARD_TIMEZONE": "America/New_York", "DASHBOARD_HOST_LABEL": "LABEL",
                    "DASHBOARD_WORLD_CLOCKS": "India=Asia/Kolkata,Singapore=Asia/Singapore,London=Europe/London"})
        base = self.serve(cfg, **self.no_network())
        payload = json.loads(self.get(base + "/api/status")[2])
        zones = payload["timezones"]
        self.assertEqual(set(zones), {"local", "india", "singapore", "london"})
        self.assertEqual({k: v["timezone"] for k, v in zones.items()},
                         {"local": "America/New_York", "india": "Asia/Kolkata",
                          "singapore": "Asia/Singapore", "london": "Europe/London"})
        self.assertEqual(zones["india"]["label"], "India")
        self.assertTrue(set(zones["local"]) >= {"time", "date", "timezone", "year", "month", "day"})
        self.assertEqual(payload["hostname"], "LABEL")

    def test_extra_world_clock_is_added_alongside_legacy_keys(self):
        base = self.serve(load({"DASHBOARD_WORLD_CLOCKS": "Tokyo=Asia/Tokyo"}), **self.no_network())
        zones = json.loads(self.get(base + "/api/status")[2])["timezones"]
        self.assertEqual(set(zones), {"local", "tokyo", "india", "singapore", "london"})
        self.assertEqual(zones["tokyo"]["timezone"], "Asia/Tokyo")

    def test_host_metrics_configured_and_disabled(self):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = json.dumps({"hostname": "BOX", "cpu": {"usagePercent": 12.5}}).encode()
        fetch = Mock(return_value=response)
        with patch.dict(globals_, {"CONFIG": load({"HOST_METRICS_URL": "http://192.0.2.10:9183/"}),
                                   "_host_cache": None, "urlopen": fetch}):
            host = backend["get_windows_metrics"]()
        fetch.assert_called_once_with("http://192.0.2.10:9183/", timeout=3)
        self.assertEqual((host["available"], host["hostname"], host["cpu"]["percent"]), (True, "BOX", 12.5))

        fetch.reset_mock()
        with patch.dict(globals_, {"CONFIG": load(), "_host_cache": None, "urlopen": fetch}):
            host = backend["get_windows_metrics"]()
        fetch.assert_not_called()
        self.assertEqual((host["available"], host["hostname"], host["disks"]), (False, "Server", []))
        self.assertEqual(host["error"], "host metrics not configured")

    def test_photos_directory_is_created_and_served(self):
        with tempfile.TemporaryDirectory() as directory:
            photos = Path(directory) / "nested" / "photos"
            base = self.serve(load({"DASHBOARD_PHOTOS_DIR": str(photos)}), **self.no_network())
            code, _, body = self.get(base + "/api/photos")
            self.assertEqual((code, json.loads(body)), (200, {"photos": []}))
            self.assertTrue(photos.is_dir())
            (photos / "photo_001.jpg").write_bytes(b"\xff\xd8jpeg")
            (photos / "notes.txt").write_text("ignored")
            self.assertEqual(json.loads(self.get(base + "/api/photos")[2]), {"photos": ["photo_001.jpg"]})
            code, kind, data = self.get(base + "/photos/photo_001.jpg")
            self.assertEqual((code, kind, data), (200, "image/jpeg", b"\xff\xd8jpeg"))

    def test_dangling_photos_link_does_not_break_the_api(self):
        # The old tracked backend/photos-ready link in a fresh clone.
        with tempfile.TemporaryDirectory() as directory:
            link = Path(directory) / "photos-ready"
            link.symlink_to(Path(directory) / "missing")
            cfg = load({"DASHBOARD_PHOTOS_DIR": str(link)})
            base = self.serve(cfg, **self.no_network())
            self.assertEqual(json.loads(self.get(base + "/api/photos")[2]), {"photos": []})
            with patch.dict(globals_, {"CONFIG": cfg}):
                self.assertIn("photos-ready", backend["ensure_photos_dir"]())


class StartupTests(unittest.TestCase):
    def run_server(self, **environ):
        env = {"PATH": os.environ.get("PATH", ""), "DASHBOARD_ENV_FILE": "", **environ}
        return subprocess.run([sys.executable, str(Path(__file__).with_name("server.py"))], env=env,
                              capture_output=True, text=True, timeout=20)

    def test_invalid_required_values_stop_startup_clearly(self):
        for environ, text in [({"DASHBOARD_PORT": "70000"}, "DASHBOARD_PORT=70000"),
                              ({"DASHBOARD_TIMEZONE": "Mars/Olympus"}, "DASHBOARD_TIMEZONE"),
                              ({"DOCKER_STATUS_ENABLED": "sometimes"}, "DOCKER_STATUS_ENABLED"),
                              ({"SERVICES": "jelifin"}, "SERVICES: unknown service jelifin")]:
            with self.subTest(environ=environ):
                result = self.run_server(**environ)
                self.assertEqual(result.returncode, 1)
                self.assertIn("Configuration error", result.stderr)
                self.assertIn(text, result.stderr)
                self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()

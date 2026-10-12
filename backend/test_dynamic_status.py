"""Phase 6: dynamic services_list / world_clocks in /api/status, next to the
legacy services / timezones keys. Fake environments only; never reads the
developer's .env or contacts a service."""
import io
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import runpy
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

import ai_assistant as A
import config
from config import ConfigError
from services import MAX_SERVICES, SERVICE_DEFINITIONS, SERVICE_ID_MAX, SERVICE_IDS, SERVICE_LABEL_MAX

with patch.dict(os.environ, {"DASHBOARD_ENV_FILE": ""}), patch("http.server.HTTPServer"), \
        redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
globals_ = backend["get_status"].__globals__

PERSONAL = {"SERVICES": "jellyfin,navidrome,metube,ollama,cloudflare,nextcloud,immich,technical_blog",
            "DASHBOARD_WORLD_CLOCKS": "India=Asia/Kolkata,Singapore=Asia/Singapore,London=Europe/London",
            "DASHBOARD_TIMEZONE": "America/New_York"}


def cfg(**environ):
    return config.load(environ=environ, env_file="")


def status_for(environ, health=None):
    """get_status() with a config, no host/Docker/weather and fixed health."""
    health = health or {}
    stubs = {
        "CONFIG": cfg(**environ),
        "get_windows_metrics": lambda: {"available": False},
        "docker_status": lambda: {"available": False, "running": 0, "containers": []},
        "check_service_http": lambda url, service=None: health.get(service, False),
        "get_weather": lambda: {"available": False},
    }
    with patch.dict(globals_, stubs):
        conf = globals_["CONFIG"]
        # Every listed service gets a health URL so the stubbed check decides.
        urls = {service: "http://192.0.2.1/" for service in conf.enabled_services}
        globals_["CONFIG"] = conf.__class__(**{**conf.__dict__, "service_urls": urls})
        return backend["get_status"]()


class FixedDatetime(datetime):
    instant = datetime(2026, 7, 1, 12, 0, tzinfo=timezone.utc)

    @classmethod
    def now(cls, tz=None):
        return cls.instant.astimezone(tz)


class ServicesListTests(unittest.TestCase):
    def test_blank_services_empty_list(self):
        payload = status_for({})
        self.assertEqual(payload["services_list"], [])
        self.assertEqual(payload["services"], dict.fromkeys(SERVICE_IDS))  # Legacy: unchanged.

    def test_one_service(self):
        payload = status_for({"SERVICES": "immich"}, {"immich": True})
        self.assertEqual(payload["services_list"], [{"id": "immich", "label": "Immich", "status": True}])

    def test_multiple_in_registry_order_with_labels_and_health(self):
        payload = status_for({"SERVICES": "technical_blog, Ollama,jellyfin"},
                             {"jellyfin": True, "ollama": False, "technical_blog": True})
        self.assertEqual(payload["services_list"], [
            {"id": "jellyfin", "label": "Jellyfin", "status": True},
            {"id": "ollama", "label": "Ollama", "status": False},
            {"id": "technical_blog", "label": "Technical Blog", "status": True},
        ])

    def test_unconfigured_services_omitted_but_legacy_object_complete(self):
        payload = status_for({"SERVICES": "jellyfin"}, {"jellyfin": False})
        self.assertEqual([item["id"] for item in payload["services_list"]], ["jellyfin"])
        self.assertEqual(payload["services"]["jellyfin"], False)
        self.assertEqual(set(payload["services"]), set(SERVICE_IDS))
        self.assertIsNone(payload["services"]["immich"])

    def test_status_is_always_boolean(self):
        payload = status_for({"SERVICES": ",".join(SERVICE_IDS)})
        self.assertTrue(all(item["status"] is False for item in payload["services_list"]))

    def test_personal_configuration_matches_legacy_values(self):
        health = {"jellyfin": True, "navidrome": True, "metube": False, "ollama": True}
        payload = status_for(PERSONAL, health)
        self.assertEqual(len(payload["services_list"]), 8)
        for item in payload["services_list"]:
            self.assertEqual(payload["services"][item["id"]], item["status"])

    def test_registry_fits_firmware_limits(self):
        self.assertLessEqual(len(SERVICE_DEFINITIONS), MAX_SERVICES)
        for service in SERVICE_DEFINITIONS:
            self.assertRegex(service.id, r"^[a-z0-9_-]{1,%d}$" % SERVICE_ID_MAX)
            self.assertTrue(1 <= len(service.label) <= SERVICE_LABEL_MAX and service.label.isascii())
        # Even every service at once stays within the firmware's list.
        self.assertLessEqual(len(status_for({"SERVICES": ",".join(SERVICE_IDS)})["services_list"]), MAX_SERVICES)

    def test_api_version(self):
        self.assertEqual(status_for({})["api_version"], 2)


class WorldClockTests(unittest.TestCase):
    def test_no_clocks(self):
        payload = status_for({})
        self.assertEqual(payload["world_clocks"], [])
        for key in ("india", "singapore", "london"):  # Legacy placeholders for firmware 1.0.x.
            self.assertEqual(payload["timezones"][key]["time"], "--:--")

    def test_one_clock(self):
        with patch.dict(globals_, {"datetime": FixedDatetime}):
            payload = status_for({"DASHBOARD_WORLD_CLOCKS": "Tokyo=Asia/Tokyo"})
        self.assertEqual(payload["world_clocks"], [{"label": "Tokyo", "timezone": "Asia/Tokyo", "time": "9:00 PM"}])

    def test_multiple_in_config_order_and_legacy_keys(self):
        with patch.dict(globals_, {"datetime": FixedDatetime}):
            payload = status_for(PERSONAL)
        self.assertEqual([c["label"] for c in payload["world_clocks"]], ["India", "Singapore", "London"])
        self.assertEqual(payload["world_clocks"][0]["time"], "5:30 PM")
        # Legacy keys keep the same values for firmware 1.0.x.
        for clock in payload["world_clocks"]:
            self.assertEqual(payload["timezones"][clock["label"].lower()]["time"], clock["time"])

    def test_dst_aware(self):
        environ = {"DASHBOARD_WORLD_CLOCKS": "New York=America/New_York,London=Europe/London"}
        times = {}
        for month, instant in (("jan", datetime(2026, 1, 15, 12, 0, tzinfo=timezone.utc)),
                               ("jul", datetime(2026, 7, 15, 12, 0, tzinfo=timezone.utc))):
            FixedDatetime.instant = instant
            with patch.dict(globals_, {"datetime": FixedDatetime}):
                times[month] = [c["time"] for c in status_for(environ)["world_clocks"]]
        FixedDatetime.instant = datetime(2026, 7, 1, 12, 0, tzinfo=timezone.utc)
        self.assertEqual(times["jan"], ["7:00 AM", "12:00 PM"])   # EST, GMT.
        self.assertEqual(times["jul"], ["8:00 AM", "1:00 PM"])    # EDT, BST.

    def test_duplicate_timezones_allowed_with_distinct_labels(self):
        clocks = cfg(DASHBOARD_WORLD_CLOCKS="Home=Europe/London,Office=Europe/London").world_clocks
        self.assertEqual([c.label for c in clocks], ["Home", "Office"])

    def test_rejected(self):
        for text in ["London=Mars/Base", "London=Europe/London,London=Europe/Paris", "london=Europe/London,LONDON=Asia/Tokyo",
                     "=Europe/London", "London=", "L" * 21 + "=Europe/London", "Bad/Label=Europe/London",
                     ",".join(f"C{i}=Europe/London" for i in range(9))]:
            with self.subTest(text), self.assertRaises(ConfigError):
                cfg(DASHBOARD_WORLD_CLOCKS=text)

    def test_limits_accepted(self):
        self.assertEqual(len(cfg(DASHBOARD_WORLD_CLOCKS=",".join(f"C{i}=Asia/Tokyo" for i in range(8))).world_clocks), 8)
        self.assertEqual(cfg(DASHBOARD_WORLD_CLOCKS="L" * 20 + "=Asia/Tokyo").world_clocks[0].label, "L" * 20)
        self.assertEqual(cfg(DASHBOARD_WORLD_CLOCKS="  New York = America/New_York ").world_clocks[0].label,
                         "New York")


class CompatibilityTests(unittest.TestCase):
    """Old firmware reads only the legacy keys; new firmware prefers the lists."""

    def test_old_firmware_fields_unchanged(self):
        payload = status_for(PERSONAL, {"jellyfin": True})
        self.assertIsInstance(payload["services"], dict)
        self.assertEqual(payload["services"]["jellyfin"], True)
        self.assertEqual(payload["services"]["metube"], False)
        for key in ("local", "india", "singapore", "london"):
            self.assertIn("time", payload["timezones"][key])
        self.assertEqual(payload["timezones"]["local"]["timezone"], "America/New_York")

    def test_payload_is_json_and_bounded(self):
        payload = status_for(PERSONAL)
        body = json.dumps(payload)
        # The firmware reads at most 32 KB (StatusClient STATUS_MAX_BODY).
        self.assertLess(len(body), 32768)


class AiDynamicServiceTests(unittest.TestCase):
    def status(self, services_list):
        return {"hostname": "HOMESERVER", "host_available": False, "services": {}, "services_list": services_list}

    def test_context_lists_only_configured_services_with_labels(self):
        data = self.status([{"id": "jellyfin", "label": "Jellyfin", "status": True},
                            {"id": "immich", "label": "Immich", "status": False}])
        facts = A.build_facts(data, A.parse_alerts([{"id": "immich", "level": "warning"}]))
        context = A.build_context(facts)
        self.assertIn("WARNING: Immich is offline", context)
        self.assertIn("OK services: Jellyfin", context)
        for unused in ("Navidrome", "MeTube", "Ollama", "Cloudflare", "Nextcloud", "Technical Blog", "Bazarr", "MCP"):
            self.assertNotIn(unused, context)
        self.assertEqual(A.rules_action(facts), "Check the Immich service status and logs.")

    def test_list_preferred_over_legacy_object(self):
        data = self.status([{"id": "jellyfin", "label": "Jellyfin", "status": True}])
        data["services"] = {"jellyfin": False, "navidrome": True}
        facts = A.build_facts(data, [])
        self.assertEqual((facts["online"], facts["offline_other"]), (["Jellyfin"], []))

    def test_any_registry_service_may_alert(self):
        self.assertEqual(A.parse_alerts([{"id": "technical_blog", "level": "warning"}])[0]["id"], "technical_blog")
        for bad in ("plex", "Jellyfin", "", "ram2"):
            with self.subTest(bad), self.assertRaises(A.RequestError):
                A.parse_alerts([{"id": bad, "level": "warning"}])

    def test_no_services(self):
        facts = A.build_facts(self.status([]), [])
        self.assertEqual((facts["online"], facts["offline_other"]), ([], []))
        self.assertNotIn("services", A.build_context(facts).lower())

    def test_malformed_list_items_ignored(self):
        data = self.status([{"id": "jellyfin", "label": "Jellyfin", "status": True}, "x", {"id": "plex"}, None])
        self.assertEqual(A.build_facts(data, [])["online"], ["Jellyfin"])


if __name__ == "__main__":
    unittest.main()

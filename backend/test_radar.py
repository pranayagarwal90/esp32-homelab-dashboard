"""Radar tests: provider and basemap HTTP are always faked; never contacts
RainViewer or OpenStreetMap."""
import io
import json
import runpy
import socket
import tempfile
import threading
import unittest
import unittest.mock
from contextlib import redirect_stdout
from http.server import HTTPServer
from pathlib import Path
from unittest.mock import patch
from urllib.request import urlopen

from PIL import Image, ImageDraw

import radar

with patch("http.server.HTTPServer"), redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
globals_ = backend["get_status"].__globals__

LAT, LON = 39.04, -77.49
HOST = "https://tilecache.example"
NOW = 1791563000


def png(mode, color, blob=None):
    image = Image.new(mode, (256, 256), color)
    if blob:
        ImageDraw.Draw(image).ellipse((60, 60, 200, 200), fill=blob)
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    return buffer.getvalue()


RADAR_TILE = png("RGBA", (0, 0, 0, 0), (40, 120, 220, 120))
MAP_TILE = png("RGB", (235, 230, 220), (170, 210, 160))


def maps(times, nowcast=()):
    return {
        "version": "2.0", "generated": NOW, "host": HOST,
        "radar": {
            "past": [{"time": t, "path": f"/v2/radar/p{t}"} for t in times],
            "nowcast": [{"time": t, "path": f"/v2/radar/n{t}"} for t in nowcast],
        },
    }


class FakeHttp:
    """Answers metadata, radar tiles and basemap tiles; records every URL."""

    def __init__(self, metadata):
        self.metadata = metadata
        self.calls = []
        self.radar_tile = RADAR_TILE
        self.fail_metadata = None
        self.map_status = 200

    def __call__(self, url, headers):
        self.calls.append((url, dict(headers)))
        if url == radar.RAINVIEWER_MAPS_URL:
            if self.fail_metadata:
                raise self.fail_metadata
            return 200, {}, json.dumps(self.metadata).encode()
        if url.startswith(HOST):
            return 200, {}, self.radar_tile
        if url.startswith("https://tile.openstreetmap.org/"):
            if self.map_status == 304:
                return 304, {}, b""
            return 200, {"ETag": '"abc"', "Cache-Control": "max-age=600"}, MAP_TILE
        raise AssertionError("unexpected URL " + url)

    def count(self, prefix):
        return sum(1 for url, _ in self.calls if url.startswith(prefix))


class Clock:
    def __init__(self, now=NOW):
        self.now = now

    def __call__(self):
        return self.now


class RadarTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.http = FakeHttp(maps(range(NOW - 7800, NOW, 600)))  # 13 past frames.
        self.clock = Clock()

    def service(self):
        return radar.RadarService(self.tmp.name, LAT, LON, "America/New_York",
                                  fetch=self.http, clock=self.clock)


class FrameSelectionTests(unittest.TestCase):
    def test_latest_six_past_frames_oldest_first(self):
        times = list(range(1000, 1000 + 13 * 600, 600))
        host, frames = radar.select_frames(maps(times))
        self.assertEqual(host, HOST)
        self.assertEqual([t for t, _ in frames], times[-6:])
        self.assertEqual(frames[0][1], f"/v2/radar/p{times[-6]}")

    def test_fewer_frames_are_all_used(self):
        _, frames = radar.select_frames(maps([1000, 1600, 2200, 2800]))
        self.assertEqual(len(frames), 4)

    def test_nowcast_is_never_used(self):
        _, frames = radar.select_frames(maps([1000, 1600], nowcast=[2200, 2800, 3400]))
        self.assertEqual([t for t, _ in frames], [1000, 1600])
        self.assertTrue(all("/p" in path for _, path in frames))

    def test_malformed_entries_are_skipped(self):
        data = maps([1000])
        data["radar"]["past"] += [
            {"time": True, "path": "/v2/radar/x"}, {"time": "2000", "path": "/v2/radar/x"},
            {"time": 3000, "path": "/v2/../etc"}, {"time": 4000, "path": "http://evil/x"},
            {"time": 5000}, "junk", {"time": -1, "path": "/v2/radar/y"},
        ]
        _, frames = radar.select_frames(data)
        self.assertEqual(frames, [(1000, "/v2/radar/p1000")])

    def test_unusable_metadata_raises(self):
        for bad in (None, [], {"host": HOST}, {"host": "http://insecure", "radar": {"past": []}},
                    {"host": HOST, "radar": {"past": []}}, {"host": HOST, "radar": {"nowcast": []}}):
            with self.assertRaises(radar.RadarError):
                radar.select_frames(bad)


class GeometryTests(unittest.TestCase):
    def test_viewport_covers_the_frame(self):
        left, top, tiles = radar.viewport(LAT, LON)
        xs = {x for x, _ in tiles}
        ys = {y for _, y in tiles}
        self.assertLessEqual(min(xs) * 256, left)
        self.assertGreaterEqual((max(xs) + 1) * 256, left + radar.FRAME_WIDTH)
        self.assertLessEqual(min(ys) * 256, top)
        self.assertGreaterEqual((max(ys) + 1) * 256, top + radar.FRAME_HEIGHT)
        self.assertLessEqual(len(tiles), 6)

    def test_location_is_the_frame_centre(self):
        x, y = radar.world_pixel(LAT, LON)
        left, top, _ = radar.viewport(LAT, LON)
        self.assertAlmostEqual(x - left, radar.FRAME_WIDTH / 2, delta=1)
        self.assertAlmostEqual(y - top, radar.FRAME_HEIGHT / 2, delta=1)

    def test_antimeridian_wraps_tile_urls(self):
        self.assertEqual(radar.tile_url_x(-1), 127)
        self.assertEqual(radar.tile_url_x(128), 0)


class RefreshTests(RadarTestCase):
    def test_jpeg_frames_dimensions_and_size(self):
        svc = self.service()
        self.assertTrue(svc.refresh())
        self.assertEqual(len(svc.frames), 6)
        for _, path in svc.frames:
            data = path.read_bytes()
            self.assertLess(len(data), 30000)
            image = Image.open(io.BytesIO(data))
            self.assertEqual(image.format, "JPEG")
            self.assertEqual(image.size, (radar.FRAME_WIDTH, radar.FRAME_HEIGHT))
            self.assertFalse(image.info.get("progressive"))  # TJpg_Decoder: baseline only.

    def test_request_count_and_tile_urls(self):
        svc = self.service()
        svc.refresh()
        tiles = len(svc.tiles)
        self.assertEqual(self.http.count(radar.RAINVIEWER_MAPS_URL), 1)
        self.assertEqual(self.http.count(HOST), 6 * tiles)
        self.assertEqual(self.http.count("https://tile.openstreetmap.org/"), tiles)
        self.assertEqual(svc.stats["requests"], 1 + 7 * tiles)
        radar_urls = [url for url, _ in self.http.calls if url.startswith(HOST)]
        self.assertTrue(all(url.endswith("/2/1_1.png") and "/256/7/" in url for url in radar_urls))

    def test_unchanged_metadata_reuses_cached_frames(self):
        svc = self.service()
        svc.refresh()
        self.http.calls.clear()
        self.clock.now += 400
        self.assertTrue(svc.refresh())
        self.assertEqual(len(self.http.calls), 1)  # Metadata only.

    def test_new_frame_downloads_only_that_frame(self):
        svc = self.service()
        svc.refresh()
        oldest = svc.frames[0][1]
        self.http.metadata = maps(range(NOW - 7800, NOW + 600, 600))
        self.http.calls.clear()
        self.assertTrue(svc.refresh())
        self.assertEqual(self.http.count(HOST), len(svc.tiles))
        self.assertEqual(self.http.count("https://tile.openstreetmap.org/"), 0)  # In memory.
        self.assertEqual(svc.frames[-1][0], NOW)
        self.assertFalse(oldest.exists())  # Dropped frames are pruned.

    def test_basemap_disk_cache_and_revalidation(self):
        self.service().refresh()
        # A new process within the cache lifetime: no basemap requests.
        self.http.metadata = maps(range(NOW, NOW + 1200, 600))
        self.http.calls.clear()
        self.service().refresh()
        self.assertEqual(self.http.count("https://tile.openstreetmap.org/"), 0)
        # After 7 days: conditional requests; 304 keeps the cached tile.
        self.clock.now += radar.BASEMAP_MIN_SECONDS + 1
        self.http.metadata = maps(range(NOW + 1200, NOW + 2400, 600))
        self.http.map_status = 304
        self.http.calls.clear()
        self.assertTrue(self.service().refresh())
        map_calls = [h for url, h in self.http.calls if url.startswith("https://tile.openstreetmap.org/")]
        self.assertTrue(map_calls)
        self.assertTrue(all(h.get("If-None-Match") == '"abc"' for h in map_calls))

    def test_malformed_radar_tile_skips_frames(self):
        self.http.radar_tile = b"<html>not a png</html>"
        svc = self.service()
        self.assertFalse(svc.refresh())
        self.assertIn("no frame", svc.error)
        self.assertFalse(svc.metadata()["available"])

    def test_wrong_size_tile_is_rejected(self):
        buffer = io.BytesIO()
        Image.new("RGBA", (512, 512)).save(buffer, "PNG")
        self.http.radar_tile = buffer.getvalue()
        self.assertFalse(self.service().refresh())

    def test_timeout_is_a_failed_refresh(self):
        self.http.fail_metadata = socket.timeout("timed out")
        svc = self.service()
        self.assertFalse(svc.refresh())
        self.assertIn("timed out", svc.error)

    def test_bad_metadata_json(self):
        self.http.metadata = None  # "null"
        svc = self.service()
        self.assertFalse(svc.refresh())
        self.assertIn("RadarError", svc.error)

    def test_http_fetch_identifies_itself(self):
        response = unittest.mock.MagicMock()
        response.__enter__.return_value = response
        response.status, response.headers = 200, {}
        response.read.return_value = b"{}"
        with patch.object(radar, "urlopen", return_value=response) as opened:
            radar.http_fetch("https://example.test/x", {"If-None-Match": "e"})
        request = opened.call_args.args[0]
        self.assertEqual(request.get_header("User-agent"), radar.USER_AGENT)
        self.assertEqual(request.get_header("If-none-match"), "e")
        self.assertEqual(opened.call_args.kwargs["timeout"], radar.REQUEST_TIMEOUT)


class MetadataTests(RadarTestCase):
    def test_no_cache_is_unavailable(self):
        self.http.fail_metadata = OSError("down")
        svc = self.service()
        svc.refresh()
        with patch.object(svc, "ensure_fresh"):
            payload = svc.metadata()
        self.assertEqual(payload["available"], False)
        self.assertNotIn("frames", payload)
        self.assertEqual(payload["attribution"], "RainViewer")

    def test_schema_internal_urls_and_privacy(self):
        svc = self.service()
        svc.refresh()
        with patch.object(svc, "ensure_fresh"):
            payload = svc.metadata()
        self.assertTrue(payload["available"])
        self.assertFalse(payload["stale"])
        self.assertEqual(payload["attribution"], "RainViewer")
        self.assertEqual(payload["map_attribution"], "OpenStreetMap contributors")
        self.assertEqual((payload["width"], payload["height"]), (300, 156))
        self.assertEqual(payload["utc_offset"], -4 * 3600)
        self.assertEqual(len(payload["frames"]), 6)
        for frame in payload["frames"]:
            self.assertEqual(frame["url"], f"/radar/{frame['time']}.jpg")
        text = json.dumps(payload)
        for secret in ("http", "rainviewer.com", "tilecache", "39.04", "77.49", "/v2/"):
            self.assertNotIn(secret, text)

    def test_provider_failure_serves_cached_frames_as_stale(self):
        svc = self.service()
        svc.refresh()
        self.http.fail_metadata = OSError("provider down")
        self.clock.now += 400
        self.assertFalse(svc.refresh())
        with patch.object(svc, "ensure_fresh"):
            payload = svc.metadata()
        self.assertTrue(payload["available"])
        self.assertTrue(payload["stale"])
        self.assertEqual(len(payload["frames"]), 6)
        self.assertIsNotNone(svc.frame_path(f"{payload['frames'][0]['time']}.jpg"))

    def test_stale_after_long_time_without_refresh(self):
        svc = self.service()
        svc.refresh()
        self.clock.now += radar.STALE_SECONDS + 1
        with patch.object(svc, "ensure_fresh"):
            self.assertTrue(svc.metadata()["stale"])

    def test_frame_path_only_serves_current_frames(self):
        svc = self.service()
        svc.refresh()
        stamp = svc.frames[0][0]
        self.assertEqual(svc.frame_path(f"{stamp}.jpg"), svc.frames[0][1])
        for bad in ("../server.py", f"{stamp}.png", "123456789.jpg", "", None, f"{stamp}.jpg/x"):
            self.assertIsNone(svc.frame_path(bad))

    def test_restart_serves_disk_frames_as_stale(self):
        self.service().refresh()
        svc = self.service()
        with patch.object(svc, "ensure_fresh"):
            payload = svc.metadata()
        self.assertTrue(payload["available"])
        self.assertTrue(payload["stale"])

    def test_metadata_never_blocks_on_the_provider(self):
        release = threading.Event()
        original = self.http.__call__

        def slow(url, headers):
            release.wait(5)
            return original(url, headers)

        svc = radar.RadarService(self.tmp.name, LAT, LON, "America/New_York",
                                 fetch=slow, clock=self.clock)
        payload = svc.metadata()  # Starts the background refresh.
        self.assertEqual(payload["available"], False)
        self.assertTrue(payload["updating"])
        self.assertFalse(svc.ensure_fresh())  # Single flight.
        release.set()
        for _ in range(100):
            if not svc.refreshing:
                break
            threading.Event().wait(0.05)
        self.assertTrue(svc.metadata()["available"])

    def test_refresh_cadence(self):
        svc = self.service()
        with patch("threading.Thread") as thread:
            self.assertTrue(svc.ensure_fresh())
            thread.return_value.start.assert_called_once()
        svc.refreshing = False
        svc.last_attempt, svc.last_ok = self.clock.now, True
        self.clock.now += radar.REFRESH_SECONDS - 1
        self.assertFalse(svc.ensure_fresh())
        svc.last_ok = False  # Failed: retry sooner.
        self.clock.now = svc.last_attempt + radar.RETRY_SECONDS
        with patch("threading.Thread"):
            self.assertTrue(svc.ensure_fresh())


class EndpointTests(RadarTestCase):
    def test_routes_and_status_unchanged(self):
        svc = self.service()
        svc.refresh()
        server = HTTPServer(("127.0.0.1", 0), backend["Handler"])
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        base = f"http://127.0.0.1:{server.server_port}"
        with patch.dict(globals_, {"RADAR": svc}), patch.object(svc, "ensure_fresh"):
            with urlopen(base + "/api/radar", timeout=5) as response:
                payload = json.loads(response.read())
            self.assertTrue(payload["available"])
            with urlopen(base + payload["frames"][-1]["url"], timeout=5) as response:
                self.assertEqual(response.headers["Content-Type"], "image/jpeg")
                self.assertEqual(response.read()[:2], b"\xff\xd8")
            for bad in ("/radar/1.jpg", "/radar/..%2Fserver.py", "/radar/"):
                with self.assertRaises(Exception):
                    urlopen(base + bad, timeout=5)

    def test_status_payload_has_no_radar(self):
        stubs = {
            "get_windows_metrics": lambda: {"available": True},
            "docker_status": lambda: {"available": True, "containers": []},
            "service_status": lambda docker: {},
            "get_weather": lambda: {"available": False},
        }
        svc = self.service()
        with patch.dict(globals_, {**stubs, "RADAR": svc}), patch.object(svc, "metadata") as meta:
            status = backend["get_status"]()
            meta.assert_not_called()
        self.assertEqual(set(status), {
            "hostname", "host_available", "uptime_hours", "cpu", "memory", "disks", "gpu",
            "wifi", "ethernet", "docker", "services", "timezones", "weather", "timestamp"})


if __name__ == "__main__":
    unittest.main()

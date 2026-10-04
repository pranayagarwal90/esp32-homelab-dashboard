"""Weather tests use provider fixtures; never contact production."""
import io
import json
from datetime import datetime
from pathlib import Path
import runpy
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock, patch
from urllib.parse import parse_qs, urlsplit
from zoneinfo import ZoneInfo

with patch("http.server.HTTPServer"), redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
get_weather = backend["get_weather"]
globals_ = get_weather.__globals__
solar_times = backend["solar_times"]
ZONE = ZoneInfo("America/New_York")


def fixture(day="2026-10-02"):
    return {"current": {"temperature_2m": 20.2, "weather_code": 2},
            "daily": {"time": [day], "temperature_2m_max": [25.3],
                      "temperature_2m_min": [12.1],
                      "sunrise": [day + "T07:10"], "sunset": [day + "T18:45"]}}


class WeatherTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime(2026, 10, 2, 12, tzinfo=ZONE).timestamp()
        state = patch.dict(globals_, {"_weather_cache": None, "_weather_cache_time": 0,
                                     "_weather_cache_day": None})
        state.start()
        self.addCleanup(state.stop)
        clock_patch = patch.object(globals_["time"], "time", return_value=self.now)
        self.clock = clock_patch.start()
        self.addCleanup(clock_patch.stop)

    def provider(self, data):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = json.dumps(data).encode()
        return patch.dict(globals_, {"urlopen": Mock(return_value=response)})

    def test_schema_and_single_cached_request(self):
        with self.provider(fixture()):
            weather = get_weather()
            self.assertIs(get_weather(), weather)
            request = globals_["urlopen"]
            request.assert_called_once()
            params = parse_qs(urlsplit(request.call_args.args[0]).query)
            self.assertEqual(params["daily"], ["temperature_2m_max,temperature_2m_min,sunrise,sunset"])
            self.assertEqual(params["timezone"], ["America/New_York"])
        self.assertEqual(weather["temperature_c"], 20.2)
        self.assertEqual(weather["condition"], "Partly cloudy")
        self.assertTrue(weather["available"])
        for field in ("sunrise_timestamp", "sunset_timestamp", "solar_day_start", "solar_day_end", "solar_valid_until"):
            self.assertIs(type(weather[field]), int)
        sunrise = datetime.fromtimestamp(weather["sunrise_timestamp"], ZONE)
        self.assertEqual((sunrise.hour, sunrise.minute), (7, 10))

    def test_dst_day_lengths_and_offsets(self):
        for day, hours in [("2026-03-08", 23), ("2026-11-01", 25)]:
            with self.subTest(day=day):
                solar = solar_times(fixture(day)["daily"])
                self.assertEqual(solar["solar_day_end"] - solar["solar_day_start"], hours * 3600)
                expected = datetime.fromisoformat(day + "T07:10").replace(tzinfo=ZONE)
                self.assertEqual(solar["sunrise_timestamp"], int(expected.timestamp()))

    def test_optional_bad_solar_preserves_weather(self):
        for bad in [None, [], [None], ["invalid"], ["2026-10-01T07:10"]]:
            with self.subTest(value=bad):
                data = fixture()
                data["daily"]["sunrise"] = bad
                with patch.dict(globals_, {"_weather_cache": None}), self.provider(data):
                    weather = get_weather()
                self.assertTrue(weather["available"])
                self.assertEqual(weather["high_c"], 25.3)
                self.assertNotIn("sunrise_timestamp", weather)
        data = fixture()
        del data["daily"]["sunset"]
        self.assertEqual(solar_times(data["daily"]), {})

    def test_midnight_refreshes_recent_cache(self):
        self.clock.return_value = datetime(2026, 10, 2, 23, 59, tzinfo=ZONE).timestamp()
        with self.provider(fixture()):
            get_weather()
            self.clock.return_value += 120
            globals_["urlopen"].return_value.read.return_value = json.dumps(fixture("2026-10-03")).encode()
            next_day = get_weather()
            self.assertEqual(globals_["urlopen"].call_count, 2)
        self.assertEqual(datetime.fromtimestamp(next_day["solar_day_start"], ZONE).day, 3)

    def test_weather_failure_keeps_stale_cache(self):
        with self.provider(fixture()):
            cached = get_weather()
            self.clock.return_value += 901
            with patch.dict(globals_, {"urlopen": Mock(side_effect=OSError("offline"))}):
                self.assertIs(get_weather(), cached)
                globals_["_weather_cache"] = None
                self.assertFalse(get_weather()["available"])

    def test_next_sunrise_covers_midnight_and_dst(self):
        daily = fixture("2026-10-31")["daily"]
        daily["sunrise"].append("2026-11-01T07:10")
        solar = solar_times(daily)
        expected = datetime(2026, 11, 1, 7, 10, tzinfo=ZONE)
        self.assertEqual(solar["solar_valid_until"], int(expected.timestamp()))
        daily["sunrise"][1] = None
        solar = solar_times(daily)
        self.assertEqual(solar["solar_valid_until"], solar["solar_day_end"])


if __name__ == "__main__":
    unittest.main()

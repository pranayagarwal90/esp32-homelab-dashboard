"""Weather tests use provider fixtures; never contact production."""
import io
import json
from datetime import datetime
import os
from pathlib import Path
import runpy
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock, patch
from urllib.parse import parse_qs, urlsplit
from zoneinfo import ZoneInfo

import config

# Hermetic: never read the developer's .env.
with patch.dict(os.environ, {"DASHBOARD_ENV_FILE": ""}), patch("http.server.HTTPServer"), \
        redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
get_weather = backend["get_weather"]
globals_ = get_weather.__globals__
# A fixture location (not a real deployment) in the zone the fixtures use.
WEATHER_CONFIG = config.load(environ={"DASHBOARD_TIMEZONE": "America/New_York",
                                      "WEATHER_LATITUDE": "40.71", "WEATHER_LONGITUDE": "-74.01"},
                             env_file="")
solar_times = backend["solar_times"]
weather_payload = backend["weather_payload"]
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
                                     "_weather_cache_day": None, "CONFIG": WEATHER_CONFIG})
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
            self.assertEqual(params["daily"], ["temperature_2m_max,temperature_2m_min,sunrise,sunset,"
                                               "precipitation_probability_max"])
            self.assertEqual(params["hourly"], ["temperature_2m,precipitation_probability,weather_code"])
            self.assertEqual(params["current"], ["temperature_2m,apparent_temperature,relative_humidity_2m,"
                                                 "precipitation,weather_code,wind_speed_10m"])
            self.assertEqual(params["temperature_unit"], ["celsius"])
            self.assertEqual(params["timezone"], ["America/New_York"])
            self.assertEqual((params["latitude"], params["longitude"]), (["40.71"], ["-74.01"]))
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


def rich_fixture(day="2026-10-02"):
    data = fixture(day)
    data["current"].update({"apparent_temperature": 18.64, "relative_humidity_2m": 56.4,
                            "precipitation": 0.25, "wind_speed_10m": 13.06})
    data["daily"]["precipitation_probability_max"] = [40]
    hours = [f"{day}T{h:02d}:00" for h in range(24)]
    data["hourly"] = {"time": hours,
                      "temperature_2m": [10.0 + h / 2 for h in range(24)],
                      "precipitation_probability": [h * 2 for h in range(24)],
                      "weather_code": [61 if h >= 14 else 2 for h in range(24)]}
    return data


class RichWeatherTests(unittest.TestCase):
    def setUp(self):
        self.now = datetime(2026, 10, 2, 12, 30, tzinfo=ZONE).timestamp()
        state = patch.dict(globals_, {"_weather_cache": None, "_weather_cache_time": 0,
                                     "_weather_cache_day": None, "CONFIG": WEATHER_CONFIG})
        state.start()
        self.addCleanup(state.stop)
        clock_patch = patch.object(globals_["time"], "time", return_value=self.now)
        self.clock = clock_patch.start()
        self.addCleanup(clock_patch.stop)

    def fetch(self, data):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = json.dumps(data).encode()
        with patch.dict(globals_, {"urlopen": Mock(return_value=response)}):
            weather = get_weather()
            self.assertEqual(globals_["urlopen"].call_count, 1)
            cache = {key: globals_[key] for key in ("_weather_cache", "_weather_cache_time", "_weather_cache_day")}
        globals_.update(cache)  # patch.dict restores the whole module dict on exit.
        return weather

    def test_current_details(self):
        payload = weather_payload(self.fetch(rich_fixture()), self.now)
        self.assertEqual(payload["temperature_c"], 20.2)
        self.assertEqual(payload["feels_like_c"], 18.6)
        self.assertEqual(payload["humidity"], 56)
        self.assertEqual(payload["wind_kmh"], 13.1)
        self.assertEqual(payload["precip_mm"], 0.2)  # round(0.25, 1) -> 0.2 (banker's).
        self.assertEqual(payload["high_c"], 25.3)
        self.assertEqual(payload["low_c"], 12.1)
        self.assertEqual(payload["precip_probability_max"], 40)
        # Current hour (12:00) chance.
        self.assertEqual(payload["precip_probability"], 24)

    def test_hourly_compact_list(self):
        weather = self.fetch(rich_fixture())
        payload = weather_payload(weather, self.now)
        hourly = payload["hourly"]
        self.assertEqual(len(hourly), 8)
        self.assertEqual([item["h"] for item in hourly], list(range(12, 20)))  # Starts at the current hour.
        self.assertEqual(hourly[0], {"h": 12, "t": 16.0, "p": 24, "c": 2})
        self.assertEqual(hourly[2]["c"], 61)
        self.assertEqual(set(hourly[0]), {"h", "t", "p", "c"})
        # Private cache data never reaches the device; the cache is not mutated.
        self.assertNotIn("_hourly", payload)
        self.assertIn("_hourly", weather)
        self.assertNotIn("hourly", weather)
        # Later in the same cache window the list moves on, with no new request.
        later = weather_payload(weather, self.now + 2 * 3600)
        self.assertEqual(later["hourly"][0]["h"], 14)
        # Near the end of the source data the list is shorter than 8.
        late = weather_payload(weather, datetime(2026, 10, 2, 21, 5, tzinfo=ZONE).timestamp())
        self.assertEqual([item["h"] for item in late["hourly"]], [21, 22, 23])
        # Past the end: empty list and no current-hour chance.
        done = weather_payload(weather, datetime(2026, 10, 3, 1, 0, tzinfo=ZONE).timestamp())
        self.assertEqual(done["hourly"], [])
        self.assertNotIn("precip_probability", done)
        # Payload stays compact.
        self.assertLess(len(json.dumps(payload)), 800)

    def test_missing_and_bad_hourly_fields(self):
        data = rich_fixture()
        del data["hourly"]["precipitation_probability"]
        data["hourly"]["weather_code"][13] = None
        data["hourly"]["temperature_2m"][14] = "warm"
        hourly = weather_payload(self.fetch(data), self.now)["hourly"]
        self.assertEqual(hourly[0], {"h": 12, "t": 16.0, "c": 2})
        self.assertEqual(hourly[1], {"h": 13, "t": 16.5})
        self.assertEqual(hourly[2]["h"], 15)  # Hour 14 skipped: bad temperature.
        self.assertNotIn("precip_probability", weather_payload(globals_["_weather_cache"], self.now))
        for broken in (None, {}, {"time": "x"}, {"time": ["2026-10-02T12:00"]}, []):
            with self.subTest(hourly=broken):
                data = rich_fixture()
                data["hourly"] = broken
                globals_["_weather_cache"] = None
                weather = self.fetch(data)
                self.assertTrue(weather["available"])
                self.assertNotIn("hourly", weather_payload(weather, self.now))
        # No hourly block at all (older fixture) and missing current extras.
        globals_["_weather_cache"] = None
        weather = self.fetch(fixture())
        payload = weather_payload(weather, self.now)
        for key in ("hourly", "feels_like_c", "humidity", "wind_kmh", "precip_mm",
                    "precip_probability", "precip_probability_max"):
            self.assertNotIn(key, payload)
        self.assertEqual(payload["temperature_c"], 20.2)

    def test_bad_current_extras_are_omitted(self):
        data = rich_fixture()
        data["current"].update({"apparent_temperature": None, "relative_humidity_2m": "wet",
                                "wind_speed_10m": True})
        data["daily"]["precipitation_probability_max"] = []
        payload = weather_payload(self.fetch(data), self.now)
        for key in ("feels_like_c", "humidity", "wind_kmh", "precip_probability_max"):
            self.assertNotIn(key, payload)
        self.assertEqual(payload["precip_mm"], 0.2)

    def test_existing_keys_and_solar_unchanged(self):
        rich = weather_payload(self.fetch(rich_fixture()), self.now)
        globals_["_weather_cache"] = None
        plain = self.fetch(fixture())
        for key, value in plain.items():
            if key.startswith("_"):
                continue
            self.assertEqual(rich[key], value, key)
        for key in ("available", "temperature_c", "high_c", "low_c", "condition", "weather_code",
                    "sunrise_timestamp", "sunset_timestamp", "solar_day_start", "solar_day_end",
                    "solar_valid_until"):
            self.assertIn(key, rich)

    def test_failure_keeps_cache_and_unavailable_payload(self):
        cached = self.fetch(rich_fixture())
        self.clock.return_value += 901
        with patch.dict(globals_, {"urlopen": Mock(side_effect=OSError("offline"))}):
            self.assertIs(get_weather(), cached)  # Stale cache, hourly included.
            self.assertEqual(len(weather_payload(get_weather(), self.clock.return_value)["hourly"]), 8)
            globals_["_weather_cache"] = None
            unavailable = weather_payload(get_weather(), self.clock.return_value)
        self.assertFalse(unavailable["available"])
        self.assertNotIn("hourly", unavailable)


class WeatherConfigTests(unittest.TestCase):
    def setUp(self):
        state = patch.dict(globals_, {"_weather_cache": None, "_weather_cache_time": 0,
                                     "_weather_cache_day": None,
                                     "urlopen": Mock(side_effect=AssertionError("network call"))})
        state.start()
        self.addCleanup(state.stop)

    def weather_with(self, environ):
        with patch.dict(globals_, {"CONFIG": config.load(environ=environ, env_file="")}):
            return get_weather()

    def test_blank_coordinates_disable_weather_without_a_request(self):
        for environ in ({}, {"WEATHER_LATITUDE": "", "WEATHER_LONGITUDE": ""}):
            with self.subTest(environ=environ):
                self.assertEqual(self.weather_with(environ),
                                 {"available": False, "error": "weather not configured"})
        globals_["urlopen"].assert_not_called()

    def test_one_coordinate_or_invalid_coordinates_disable_weather(self):
        for environ in ({"WEATHER_LATITUDE": "40.7"}, {"WEATHER_LONGITUDE": "-74"},
                        {"WEATHER_LATITUDE": "91", "WEATHER_LONGITUDE": "0"},
                        {"WEATHER_LATITUDE": "north", "WEATHER_LONGITUDE": "0"}):
            with self.subTest(environ=environ):
                self.assertFalse(self.weather_with(environ)["available"])
        globals_["urlopen"].assert_not_called()

    def test_disabled_weather_payload_has_no_forecast(self):
        payload = backend["weather_payload"](self.weather_with({}), 0)
        self.assertEqual(payload, {"available": False, "error": "weather not configured"})


if __name__ == "__main__":
    unittest.main()

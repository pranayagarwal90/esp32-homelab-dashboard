from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from http.client import HTTPConnection, HTTPSConnection
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, time as datetime_time, timedelta
from pathlib import Path
from urllib.parse import unquote, urlencode, urlsplit
from urllib.request import urlopen
from zoneinfo import ZoneInfo
import json
import subprocess
import time

from ai_assistant import AiAssistant, RequestError, default_engines
from services import SERVICE_IDS, SERVICES_BY_ID
import config

# All deployment-specific values (location, addresses, paths) come from
# config: the process environment, then an optional .env, then generic defaults.
try:
    CONFIG = config.load()
except config.ConfigError as error:
    raise SystemExit(f"Configuration error: {error}") from None

HOST_METRICS_CACHE_SECONDS = 2

WEATHER_CACHE_SECONDS = 900
WEATHER_HOURLY_MAX = 8

_weather_cache = None
_weather_cache_time = 0
_weather_cache_day = None

_host_cache = None
_host_cache_time = 0


def bytes_to_gb(value):
    if value is None:
        return 0.0
    return round(float(value) / (1024 ** 3), 2)


def get_windows_metrics():
    global _host_cache, _host_cache_time

    if not CONFIG.host_metrics_url:
        return host_unavailable("host metrics not configured")

    now = time.time()
    if _host_cache and now - _host_cache_time < HOST_METRICS_CACHE_SECONDS:
        return _host_cache

    try:
        with urlopen(CONFIG.host_metrics_url, timeout=3) as response:
            raw = json.loads(response.read())

        memory = raw.get("memory", {})
        disks = []
        for disk in raw.get("disks", []):
            disks.append(
                {
                    "name": disk.get("name", ""),
                    "label": disk.get("label", ""),
                    "used_gb": bytes_to_gb(disk.get("usedBytes")),
                    "total_gb": bytes_to_gb(disk.get("totalBytes")),
                    "free_gb": bytes_to_gb(disk.get("freeBytes")),
                    "percent": round(float(disk.get("usagePercent") or 0), 1),
                }
            )

        gpu_list = raw.get("gpus", [])
        gpu = gpu_list[0] if gpu_list else {}

        wifi = {}
        ethernet = {}
        for network in raw.get("networks", []):
            if str(network.get("status", "")).lower() != "up":
                continue
            if network.get("type") == "wifi" and not wifi:
                wifi = network
            elif network.get("type") == "ethernet" and not ethernet:
                ethernet = network

        result = {
            "available": True,
            "hostname": raw.get("hostname", CONFIG.host_label),
            "uptime_hours": round(float(raw.get("uptimeSeconds") or 0) / 3600, 1),
            "cpu": {
                "percent": round(float(raw.get("cpu", {}).get("usagePercent") or 0), 1),
            },
            "memory": {
                "used_gb": bytes_to_gb(memory.get("usedBytes")),
                "total_gb": bytes_to_gb(memory.get("totalBytes")),
                "free_gb": bytes_to_gb(memory.get("freeBytes")),
                "percent": round(float(memory.get("usagePercent") or 0), 1),
            },
            "disks": disks,
            "gpu": {
                "name": gpu.get("name", ""),
                "vendor": gpu.get("vendor", ""),
                "percent": round(float(gpu.get("usagePercent") or 0), 1),
            },
            "wifi": {
                "available": bool(wifi),
                "name": wifi.get("name", ""),
                "link_mbps": round(float(wifi.get("linkMbps") or 0), 1),
                "receive_mbps": round(float(wifi.get("receiveMbps") or 0), 2),
                "send_mbps": round(float(wifi.get("sendMbps") or 0), 2),
                "signal_percent": wifi.get("signalPercent"),
            },
            "ethernet": {
                "available": bool(ethernet),
                "name": ethernet.get("name", ""),
                "link_mbps": round(float(ethernet.get("linkMbps") or 0), 1),
                "receive_mbps": round(float(ethernet.get("receiveMbps") or 0), 2),
                "send_mbps": round(float(ethernet.get("sendMbps") or 0), 2),
            },
        }

        _host_cache = result
        _host_cache_time = now
        return result

    except Exception as e:
        if _host_cache:
            stale = dict(_host_cache)
            stale["available"] = False
            stale["error"] = str(e)
            return stale

        return host_unavailable(str(e))


def host_unavailable(error):
    """The host block when metrics are disabled or have never been read."""
    return {
        "available": False,
        "hostname": CONFIG.host_label,
        "uptime_hours": 0,
        "cpu": {"percent": 0},
        "memory": {"used_gb": 0, "total_gb": 0, "free_gb": 0, "percent": 0},
        "disks": [],
        "gpu": {"name": "", "vendor": "", "percent": 0},
        "wifi": {
            "available": False,
            "name": "",
            "link_mbps": 0,
            "receive_mbps": 0,
            "send_mbps": 0,
            "signal_percent": None,
        },
        "ethernet": {
            "available": False,
            "name": "",
            "link_mbps": 0,
            "receive_mbps": 0,
            "send_mbps": 0,
        },
        "error": error,
    }


def docker_status():
    if not CONFIG.docker_status_enabled:
        return {
            "available": False,
            "running": 0,
            "containers": [],
            "error": "docker status disabled",
        }

    try:
        result = subprocess.run(
            ["docker", "ps", "--format", "{{.Names}}|{{.Status}}"],
            capture_output=True,
            text=True,
            timeout=3,
        )

        if result.returncode != 0:
            return {
                "available": False,
                "running": 0,
                "containers": [],
                "error": result.stderr.strip(),
            }

        containers = []
        for line in result.stdout.strip().splitlines():
            if not line:
                continue
            name, container_status = line.split("|", 1)
            containers.append(
                {
                    "name": name,
                    "status": container_status,
                    "running": container_status.lower().startswith("up"),
                }
            )

        return {
            "available": True,
            "running": len(containers),
            "containers": containers,
        }

    except Exception as e:
        return {
            "available": False,
            "running": 0,
            "containers": [],
            "error": str(e),
        }


SERVICE_HEALTH_TIMEOUT_SECONDS = 0.5


def check_service_http(url, service=None):
    connection = None
    try:
        endpoint = urlsplit(url)
        if (endpoint.scheme not in {"http", "https"} or not endpoint.hostname
                or endpoint.username is not None or endpoint.password is not None
                or endpoint.fragment):
            return False
        connection_class = HTTPSConnection if endpoint.scheme == "https" else HTTPConnection
        connection = connection_class(
            endpoint.hostname, endpoint.port, timeout=SERVICE_HEALTH_TIMEOUT_SECONDS
        )
        path = endpoint.path or "/"
        if endpoint.query:
            path += "?" + endpoint.query
        connection.request("GET", path)
        response = connection.getresponse()
        if not 200 <= response.status < 300:
            return False
        # Jellyfin's native Windows health endpoint returns plain-text Healthy.
        # A generic successful HTML response must not count as Jellyfin health.
        if service == "jellyfin":
            body = response.read(4097)
            return len(body) <= 4096 and body.strip() == b"Healthy"
        # Read only small health responses, never application pages.
        if service in {"nextcloud", "immich", "ollama"}:
            body = response.read(4097)
            if len(body) > 4096:
                return False
            data = json.loads(body)
            if service == "nextcloud":
                return (data.get("installed") is True
                        and data.get("maintenance") is False
                        and data.get("needsDbUpgrade") is False)
            if service == "immich":
                return data.get("res") == "pong"
            return isinstance(data.get("version"), str) and bool(data["version"].strip())
        return True
    except Exception:
        return False
    finally:
        if connection is not None:
            connection.close()


def service_status(docker):
    """Every known service id: True/False for services enabled in SERVICES
    (health URL if set, else a running Docker container whose name matches;
    neither = down), None for the rest (unknown: never checked, never down)."""
    running_names = [
        c["name"].lower()
        for c in docker.get("containers", [])
        if c.get("running")
    ]

    def docker_match(service_id):
        terms = SERVICES_BY_ID[service_id].docker_names
        return any(term in name for term in terms for name in running_names)

    services = dict.fromkeys(SERVICE_IDS)
    with ThreadPoolExecutor(max_workers=len(SERVICE_IDS)) as executor:
        checks = {}
        for service_id in CONFIG.enabled_services:
            url = CONFIG.service_urls.get(service_id)
            if url:
                checks[service_id] = executor.submit(check_service_http, url, service_id)
            else:
                services[service_id] = docker_match(service_id)
        for service_id, check in checks.items():
            try:
                services[service_id] = bool(check.result())
            except Exception:
                services[service_id] = False
    return services


# World-clock keys the current firmware reads by name (StatusClient.cpp).
# Until world clocks are data-driven there, each one that is not configured
# is sent as a "--:--" placeholder so the clocks page never shows garbage.
LEGACY_CLOCK_KEYS = ("india", "singapore", "london")


def get_times():
    zones = {"local": (None, CONFIG.timezone)}
    for clock in CONFIG.world_clocks:
        zones[clock.key] = (clock.label, clock.zone)

    result = {}
    for name, (label, timezone_name) in zones.items():
        now = datetime.now(ZoneInfo(timezone_name))
        result[name] = {
            "time": now.strftime("%-I:%M %p"),
            "date": now.strftime("%a, %b %-d"),
            "timezone": timezone_name,
            "year": now.year,
            "month": now.month,
            "day": now.day,
        }
        if label:
            result[name]["label"] = label

    for name in LEGACY_CLOCK_KEYS:
        result.setdefault(name, {"time": "--:--", "date": "", "timezone": None})

    return result


def weather_description(code):
    descriptions = {
        0: "Clear",
        1: "Mostly clear",
        2: "Partly cloudy",
        3: "Cloudy",
        45: "Fog",
        48: "Fog",
        51: "Light drizzle",
        53: "Drizzle",
        55: "Heavy drizzle",
        61: "Light rain",
        63: "Rain",
        65: "Heavy rain",
        71: "Light snow",
        73: "Snow",
        75: "Heavy snow",
        80: "Rain showers",
        81: "Rain showers",
        82: "Heavy showers",
        95: "Thunderstorm",
        96: "Thunderstorm",
        99: "Thunderstorm",
    }
    return descriptions.get(code, "Unknown")


def solar_times(daily):
    """Isolate optional solar parsing so a bad field cannot break weather."""
    try:
        zone = ZoneInfo(CONFIG.timezone)
        day = datetime.fromisoformat(daily["time"][0]).date()
        start = datetime.combine(day, datetime_time.min, zone)
        end = datetime.combine(day + timedelta(days=1), datetime_time.min, zone)
        values = {}
        for name in ("sunrise", "sunset"):
            event = datetime.fromisoformat(daily[name][0])
            if event.tzinfo is None:
                event = event.replace(tzinfo=zone)
            event = event.astimezone(zone)
            if event.date() != day:
                return {}
            values[name + "_timestamp"] = int(event.timestamp())
        if values["sunrise_timestamp"] >= values["sunset_timestamp"]:
            return {}
        # Cover the night across midnight without briefly selecting the full-
        # brightness fallback while the next day's status is being fetched.
        valid_until = int(end.timestamp())
        try:
            next_rise = datetime.fromisoformat(daily["sunrise"][1])
            if next_rise.tzinfo is None:
                next_rise = next_rise.replace(tzinfo=zone)
            next_rise = next_rise.astimezone(zone)
            if (next_rise.date() == day + timedelta(days=1)
                    and next_rise > end):
                valid_until = int(next_rise.timestamp())
        except (IndexError, TypeError, ValueError):
            pass
        return {**values, "solar_day_start": int(start.timestamp()),
                "solar_day_end": int(end.timestamp()),
                "solar_valid_until": valid_until}
    except (KeyError, IndexError, TypeError, ValueError):
        return {}


def _number(value):
    """A finite JSON number, or None (booleans and strings are rejected)."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    return value if value == value and abs(value) != float("inf") else None


def current_details(current, daily):
    """Optional richer current fields; each is omitted when missing or bad."""
    details = {}
    fields = (("feels_like_c", current.get("apparent_temperature"), 1),
              ("wind_kmh", current.get("wind_speed_10m"), 1),
              ("precip_mm", current.get("precipitation"), 1))
    for key, raw, digits in fields:
        value = _number(raw)
        if value is not None:
            details[key] = round(value, digits)
    humidity = _number(current.get("relative_humidity_2m"))
    if humidity is not None:
        details["humidity"] = int(round(humidity))
    try:
        chance = _number(daily["precipitation_probability_max"][0])
        if chance is not None:
            details["precip_probability_max"] = int(round(chance))
    except (KeyError, IndexError, TypeError):
        pass
    return details


def hourly_source(hourly):
    """Cached hourly forecast: (epoch, temperature, probability, code) with
    probability/code None when missing. Entries without time/temperature are
    skipped; a bad block yields an empty list."""
    entries = []
    try:
        zone = ZoneInfo(CONFIG.timezone)
        times = hourly["time"]
        temps = hourly["temperature_2m"]
        chances = hourly.get("precipitation_probability") or []
        codes = hourly.get("weather_code") or []
        for i, stamp in enumerate(times):
            try:
                moment = datetime.fromisoformat(stamp)
                temp = _number(temps[i])
            except (IndexError, TypeError, ValueError):
                continue
            if temp is None:
                continue
            if moment.tzinfo is None:
                moment = moment.replace(tzinfo=zone)
            chance = _number(chances[i]) if i < len(chances) else None
            code = _number(codes[i]) if i < len(codes) else None
            entries.append((int(moment.timestamp()), round(temp, 1),
                            None if chance is None else int(round(chance)),
                            None if code is None else int(code)))
    except (KeyError, TypeError, AttributeError):
        return []
    entries.sort()
    return entries


def weather_payload(weather, now):
    """The /api/status weather object: the cached fields (without private
    ones) plus the current-hour rain chance and the next hours, chosen at
    request time so a cached forecast never starts with a past hour."""
    payload = {key: value for key, value in weather.items() if not key.startswith("_")}
    source = weather.get("_hourly")
    if not source:
        return payload
    zone = ZoneInfo(CONFIG.timezone)
    upcoming = [entry for entry in source if entry[0] + 3600 > now][:WEATHER_HOURLY_MAX]
    hourly = []
    for epoch, temp, chance, code in upcoming:
        item = {"h": datetime.fromtimestamp(epoch, zone).hour, "t": temp}
        if chance is not None:
            item["p"] = chance
        if code is not None:
            item["c"] = code
        hourly.append(item)
    payload["hourly"] = hourly
    if upcoming and upcoming[0][0] <= now and upcoming[0][2] is not None:
        payload["precip_probability"] = upcoming[0][2]
    return payload


def get_weather():
    global _weather_cache, _weather_cache_time, _weather_cache_day

    if CONFIG.weather_location is None:
        return {"available": False, "error": "weather not configured"}

    now = time.time()
    local_day = datetime.fromtimestamp(now, ZoneInfo(CONFIG.timezone)).date()
    if (_weather_cache and now - _weather_cache_time < WEATHER_CACHE_SECONDS
            and _weather_cache_day == local_day):
        return _weather_cache

    try:
        latitude, longitude = CONFIG.weather_location
        params = urlencode(
            {
                "latitude": latitude,
                "longitude": longitude,
                "current": ("temperature_2m,apparent_temperature,relative_humidity_2m,"
                            "precipitation,weather_code,wind_speed_10m"),
                "hourly": "temperature_2m,precipitation_probability,weather_code",
                "daily": ("temperature_2m_max,temperature_2m_min,sunrise,sunset,"
                          "precipitation_probability_max"),
                "temperature_unit": "celsius",
                "timezone": CONFIG.timezone,
                "forecast_days": 2,
            }
        )

        url = "https://api.open-meteo.com/v1/forecast?" + params
        with urlopen(url, timeout=5) as response:
            data = json.loads(response.read())

        current = data["current"]
        daily = data["daily"]
        code = int(current["weather_code"])

        result = {
            "available": True,
            "temperature_c": round(current["temperature_2m"], 1),
            "high_c": round(daily["temperature_2m_max"][0], 1),
            "low_c": round(daily["temperature_2m_min"][0], 1),
            "condition": weather_description(code),
            "weather_code": code,
            **solar_times(daily),
            **current_details(current, daily),
            # Private: the full hourly forecast, sliced per request.
            "_hourly": hourly_source(data.get("hourly")),
        }

        _weather_cache = result
        _weather_cache_time = now
        _weather_cache_day = local_day
        return result

    except Exception as e:
        if _weather_cache:
            return _weather_cache
        return {"available": False, "error": str(e)}


def ensure_photos_dir():
    """Creates the photo directory when missing. Returns the reason it is
    unusable (e.g. a file or a dangling link at that path), or None."""
    try:
        CONFIG.photos_dir.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        return f"{CONFIG.photos_dir}: {error.strerror or error}"
    return None


def list_photos():
    if ensure_photos_dir():
        return []
    return sorted(
        p.name
        for p in CONFIG.photos_dir.iterdir()
        if p.is_file() and p.suffix.lower() in {".jpg", ".jpeg"}
    )


def get_status():
    host = get_windows_metrics()
    docker = docker_status()

    return {
        "hostname": host.get("hostname", CONFIG.host_label),
        "host_available": host.get("available", False),
        "uptime_hours": host.get("uptime_hours", 0),
        "cpu": host.get("cpu", {"percent": 0}),
        "memory": host.get("memory", {}),
        "disks": host.get("disks", []),
        "gpu": host.get("gpu", {}),
        "wifi": host.get("wifi", {}),
        "ethernet": host.get("ethernet", {}),
        "docker": docker,
        "services": service_status(docker),
        "timezones": get_times(),
        "weather": weather_payload(get_weather(), time.time()),
        "timestamp": int(time.time()),
    }


# Read-only AI explanations from local Ollama; context from get_status().
AI = AiAssistant(lambda: get_status(), *default_engines(CONFIG.ai))


class Handler(BaseHTTPRequestHandler):
    def send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/status":
            self.send_json(get_status())
            return

        if self.path == "/api/ai/status":
            self.send_json(AI.status())
            return

        if self.path == "/api/photos":
            self.send_json({"photos": list_photos()})
            return

        if self.path.startswith("/photos/"):
            filename = Path(unquote(self.path[len("/photos/"):])).name
            photo = CONFIG.photos_dir / filename

            if (
                not filename
                or photo.suffix.lower() not in {".jpg", ".jpeg"}
                or not photo.is_file()
            ):
                self.send_error(404)
                return

            data = photo.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(data)
            return

        self.send_error(404)

    def do_POST(self):
        if self.path != "/api/ai/explain":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = -1
        if length < 0 or length > 2048:
            self.send_json({"available": False, "error": "invalid request"}, 400)
            return
        try:
            mode, refresh, alerts = AI.parse_request(self.rfile.read(length))
        except RequestError as error:
            self.send_json({"available": False, "error": "invalid request: " + str(error)}, 400)
            return
        self.send_json(AI.explain(mode, refresh, alerts))

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    # Threaded: a slow AI request never holds up /api/status.
    server = ThreadingHTTPServer((CONFIG.bind, CONFIG.port), Handler)
    server.daemon_threads = True
    print("Homelab Dashboard API")
    print(f"Listening on {CONFIG.bind}:{CONFIG.port}")
    print("Config file:", CONFIG.env_file or "none (environment and defaults)")
    print("Timezone:", CONFIG.timezone)
    print("Weather:", "configured" if CONFIG.weather_location else "disabled")
    print("Host metrics:", CONFIG.host_metrics_url or "disabled")
    print("Docker status:", "enabled" if CONFIG.docker_status_enabled else "disabled")
    print("Services:", ", ".join(
        f"{service_id} ({'URL' if service_id in CONFIG.service_urls else 'Docker'})"
        for service_id in CONFIG.enabled_services) or "none (SERVICES is blank)")
    photos_problem = ensure_photos_dir()
    print("Photos:", CONFIG.photos_dir, f"(unusable: {photos_problem})" if photos_problem else "")
    print("AI primary:", f"{AI.primary.model} (configured)" if AI.primary else "not configured")
    print("AI fallback:", AI.fallback.model)
    for warning in CONFIG.warnings:
        print("Warning:", warning)
    print("Endpoints: /api/status, /api/photos, /photos/<file>, /api/ai/status, POST /api/ai/explain")
    server.serve_forever()

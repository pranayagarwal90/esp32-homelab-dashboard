from http.server import BaseHTTPRequestHandler, HTTPServer
from http.client import HTTPConnection, HTTPSConnection
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, time as datetime_time, timedelta
from pathlib import Path
from urllib.parse import unquote, urlencode, urlsplit
from urllib.request import urlopen
from zoneinfo import ZoneInfo
import json
import os
import subprocess
import time

BASE_DIR = Path(__file__).resolve().parent
PHOTOS_DIR = BASE_DIR / "photos-ready"

WINDOWS_METRICS_URL = "http://192.168.1.13:9183/"
HOST_METRICS_CACHE_SECONDS = 2

WEATHER_LAT = 39.04
WEATHER_LON = -77.49
WEATHER_CACHE_SECONDS = 900
LOCAL_TIMEZONE = "America/New_York"

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

    now = time.time()
    if _host_cache and now - _host_cache_time < HOST_METRICS_CACHE_SECONDS:
        return _host_cache

    try:
        with urlopen(WINDOWS_METRICS_URL, timeout=3) as response:
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
            "hostname": raw.get("hostname", "HOMESERVER"),
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

        return {
            "available": False,
            "hostname": "HOMESERVER",
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
            "error": str(e),
        }


def docker_status():
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
# Verified against this homelab's published ports and read-only HTTP probes.
# Environment variables below can override these deployment-specific defaults.
DEFAULT_SERVICE_HEALTH_URLS = {
    "jellyfin": "http://192.168.1.13:8096/health",
    "navidrome": "http://127.0.0.1:4533/ping",
    # Published 8084 -> 8081; installed MeTube healthcheck requests /.
    "metube": "http://127.0.0.1:8084/",
    "ollama": "http://127.0.0.1:11434/api/version",
    "nextcloud": "http://127.0.0.1:8080/status.php",
    "immich": "http://127.0.0.1:2283/api/server/ping",
    "technical_blog": "http://192.168.1.13:8085/",
}
SERVICE_MATCH_TERMS = {
    "jellyfin": ("jellyfin",),
    "navidrome": ("navidrome",),
    "metube": ("metube",),
    "bazarr": ("bazarr",),
    "ollama": ("ollama",),
    "cloudflare": ("cloudflared", "cloudflare"),
    "mcp": ("mcp",),
    # No assumed container identities for the new display entries.
    "nextcloud": (),
    "immich": (),
    "technical_blog": (),
}


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
    running_names = {
        c["name"].lower()
        for c in docker.get("containers", [])
        if c.get("running")
    }

    def any_match(*terms):
        return any(
            any(term in name for term in terms)
            for name in running_names
        )

    services = {}
    # No assumed hosts or ports. Configure only operator-verified health URLs.
    with ThreadPoolExecutor(max_workers=len(SERVICE_MATCH_TERMS)) as executor:
        checks = {}
        for name, terms in SERVICE_MATCH_TERMS.items():
            url = os.environ.get(
                f"SERVICE_{name.upper()}_HEALTH_URL",
                DEFAULT_SERVICE_HEALTH_URLS.get(name, ""),
            ).strip()
            if url:
                checks[name] = executor.submit(check_service_http, url, name)
            else:
                services[name] = any_match(*terms)
        for name, check in checks.items():
            try:
                services[name] = bool(check.result())
            except Exception:
                services[name] = False
    return services


def get_times():
    zones = {
        "local": LOCAL_TIMEZONE,
        "india": "Asia/Kolkata",
        "singapore": "Asia/Singapore",
        "london": "Europe/London",
    }

    result = {}
    for name, timezone_name in zones.items():
        now = datetime.now(ZoneInfo(timezone_name))
        result[name] = {
            "time": now.strftime("%-I:%M %p"),
            "date": now.strftime("%a, %b %-d"),
            "timezone": timezone_name,
            "year": now.year,
            "month": now.month,
            "day": now.day,
        }

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
        zone = ZoneInfo(LOCAL_TIMEZONE)
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


def get_weather():
    global _weather_cache, _weather_cache_time, _weather_cache_day

    now = time.time()
    local_day = datetime.fromtimestamp(now, ZoneInfo(LOCAL_TIMEZONE)).date()
    if (_weather_cache and now - _weather_cache_time < WEATHER_CACHE_SECONDS
            and _weather_cache_day == local_day):
        return _weather_cache

    try:
        params = urlencode(
            {
                "latitude": WEATHER_LAT,
                "longitude": WEATHER_LON,
                "current": "temperature_2m,weather_code",
                "daily": "temperature_2m_max,temperature_2m_min,sunrise,sunset",
                "temperature_unit": "celsius",
                "timezone": LOCAL_TIMEZONE,
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
        }

        _weather_cache = result
        _weather_cache_time = now
        _weather_cache_day = local_day
        return result

    except Exception as e:
        if _weather_cache:
            return _weather_cache
        return {"available": False, "error": str(e)}


def list_photos():
    PHOTOS_DIR.mkdir(exist_ok=True)
    return sorted(
        p.name
        for p in PHOTOS_DIR.iterdir()
        if p.is_file() and p.suffix.lower() in {".jpg", ".jpeg"}
    )


def get_status():
    host = get_windows_metrics()
    docker = docker_status()

    return {
        "hostname": host.get("hostname", "HOMESERVER"),
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
        "weather": get_weather(),
        "timestamp": int(time.time()),
    }


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

        if self.path == "/api/photos":
            self.send_json({"photos": list_photos()})
            return

        if self.path.startswith("/photos/"):
            filename = Path(unquote(self.path[len("/photos/"):])).name
            photo = PHOTOS_DIR / filename

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

    def log_message(self, format, *args):
        pass


server = HTTPServer(("0.0.0.0", 8090), Handler)
print("Homelab Dashboard API")
print("Listening on 0.0.0.0:8090")
print("Windows metrics:", WINDOWS_METRICS_URL)
print("Endpoints: /api/status, /api/photos, /photos/<file>")
server.serve_forever()

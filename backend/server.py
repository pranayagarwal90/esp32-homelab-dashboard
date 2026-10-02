from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime
from pathlib import Path
from urllib.parse import unquote, urlencode
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

_weather_cache = None
_weather_cache_time = 0

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

    return {
        "jellyfin": any_match("jellyfin"),
        "navidrome": any_match("navidrome"),
        "metube": any_match("metube"),
        "bazarr": any_match("bazarr"),
        "ollama": any_match("ollama"),
        "cloudflare": any_match("cloudflared", "cloudflare"),
        "mcp": any_match("mcp"),
    }


def get_times():
    zones = {
        "local": "America/New_York",
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


def get_weather():
    global _weather_cache, _weather_cache_time

    now = time.time()
    if _weather_cache and now - _weather_cache_time < WEATHER_CACHE_SECONDS:
        return _weather_cache

    try:
        params = urlencode(
            {
                "latitude": WEATHER_LAT,
                "longitude": WEATHER_LON,
                "current": "temperature_2m,weather_code",
                "daily": "temperature_2m_max,temperature_2m_min",
                "temperature_unit": "celsius",
                "timezone": "America/New_York",
                "forecast_days": 1,
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
        }

        _weather_cache = result
        _weather_cache_time = now
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

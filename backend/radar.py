"""Weather radar frames for the ESP32.

RainViewer past radar (the only radar left in its free API) is drawn over an
OpenStreetMap basemap around the weather location, cropped to the ESP32
viewport and cached on disk as small baseline JPEGs. The ESP32 only ever sees
local /radar/<time>.jpg URLs and never the coordinates.

Refreshes run on a background thread, started on demand by /api/radar, so the
endpoint always answers from the cache immediately.
"""
import io
import json
import math
import os
import re
import shutil
import threading
import time
from datetime import datetime
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo

from PIL import Image, ImageDraw, ImageEnhance, ImageOps

RAINVIEWER_MAPS_URL = "https://api.rainviewer.com/public/weather-maps.json"
# OSM asks for a switchable tile URL; RADAR_BASEMAP_URL overrides it.
DEFAULT_BASEMAP_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
USER_AGENT = "HomelabDashboard/1.0 (+https://github.com/pranayagarwal90/esp32-homelab-dashboard)"

RADAR_ATTRIBUTION = "RainViewer"
MAP_ATTRIBUTION = "OpenStreetMap contributors"

ZOOM = 7                 # RainViewer's free maximum; ~285 km across the viewport.
TILE_SIZE = 256
FRAME_WIDTH = 300        # ESP32 viewport (drawn 1:1 at x 10, y 38).
FRAME_HEIGHT = 156
FRAME_COUNT = 6          # Latest past frames (10 minutes apart).
COLOR_SCHEME = 2         # Universal Blue, the only scheme left in the free API.
TILE_OPTIONS = "1_1"     # Smoothed, snow in its own colours.
RADAR_ALPHA_GAIN = 2.0   # RainViewer tiles are half transparent; light rain was too faint.
JPEG_QUALITY = 80

REFRESH_SECONDS = 300    # Provider metadata at most every 5 minutes.
RETRY_SECONDS = 60       # After a failed refresh.
STALE_SECONDS = 1200     # No successful refresh for 20 minutes.
BASEMAP_MIN_SECONDS = 7 * 86400  # OSM: keep tiles at least 7 days.
REQUEST_TIMEOUT = 8
MAX_DOWNLOAD_BYTES = 2 * 1024 * 1024

_PATH_RE = re.compile(r"^/[A-Za-z0-9/_-]{1,120}$")
_FRAME_NAME_RE = re.compile(r"^(\d{9,11})\.jpg$")


class RadarError(Exception):
    pass


def http_fetch(url, headers=None):
    """GET url -> (status, headers, body). 304 is returned, other errors raise."""
    request = Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
    try:
        with urlopen(request, timeout=REQUEST_TIMEOUT) as response:
            body = response.read(MAX_DOWNLOAD_BYTES + 1)
            if len(body) > MAX_DOWNLOAD_BYTES:
                raise RadarError("response too large")
            return response.status, dict(response.headers), body
    except HTTPError as error:
        if error.code == 304:
            return 304, dict(error.headers or {}), b""
        raise


# --- Geometry ---------------------------------------------------------------------------

def world_pixel(lat, lon, zoom=ZOOM):
    """Web Mercator pixel of a coordinate at zoom (256 px tiles)."""
    scale = TILE_SIZE * 2 ** zoom
    lat = max(-85.05112878, min(85.05112878, lat))
    x = (lon + 180.0) / 360.0 * scale
    rad = math.radians(lat)
    y = (1 - math.log(math.tan(rad) + 1 / math.cos(rad)) / math.pi) / 2 * scale
    return x, y


def viewport(lat, lon, zoom=ZOOM, width=FRAME_WIDTH, height=FRAME_HEIGHT):
    """(left, top, tiles): the frame's world-pixel origin and the tiles it
    covers as (x, y) world tile positions (x not wrapped; see tile_url_x)."""
    x, y = world_pixel(lat, lon, zoom)
    left, top = int(round(x - width / 2)), int(round(y - height / 2))
    tiles = [(tx, ty)
             for ty in range(top // TILE_SIZE, (top + height - 1) // TILE_SIZE + 1)
             for tx in range(left // TILE_SIZE, (left + width - 1) // TILE_SIZE + 1)
             if 0 <= ty < 2 ** zoom]
    return left, top, tiles


def tile_url_x(tx, zoom=ZOOM):
    """A world tile column wrapped into the provider's range (antimeridian)."""
    return tx % (2 ** zoom)


def mosaic(tiles, left, top, width=FRAME_WIDTH, height=FRAME_HEIGHT, mode="RGBA"):
    """Crop of the viewport from {(x, y): image} world tiles."""
    out = Image.new(mode, (width, height))
    for (tx, ty), tile in tiles.items():
        out.paste(tile.convert(mode), (tx * TILE_SIZE - left, ty * TILE_SIZE - top))
    return out


# --- Provider metadata ------------------------------------------------------------------

def select_frames(maps, count=FRAME_COUNT):
    """(host, [(time, path)]) of the latest `count` past radar frames, oldest
    first. Only radar.past is used; nowcast is never read. Raises RadarError
    for unusable metadata."""
    if not isinstance(maps, dict):
        raise RadarError("metadata is not an object")
    host = maps.get("host")
    if not isinstance(host, str) or not re.match(r"^https://[A-Za-z0-9.-]+$", host):
        raise RadarError("bad host")
    radar = maps.get("radar")
    past = radar.get("past") if isinstance(radar, dict) else None
    if not isinstance(past, list):
        raise RadarError("no past radar")
    frames = {}
    for entry in past:
        if not isinstance(entry, dict):
            continue
        stamp, path = entry.get("time"), entry.get("path")
        if isinstance(stamp, bool) or not isinstance(stamp, int) or stamp <= 0:
            continue
        if not isinstance(path, str) or not _PATH_RE.match(path) or ".." in path:
            continue
        frames[stamp] = path
    if not frames:
        raise RadarError("no usable past frames")
    latest = sorted(frames.items())[-count:]
    return host, latest


# --- Rendering --------------------------------------------------------------------------

def style_basemap(image):
    """Dark grey map (inverted, dimmed) so radar colours stand out."""
    grey = ImageOps.grayscale(image.convert("RGB"))
    return ImageEnhance.Brightness(ImageOps.invert(grey)).enhance(0.75).convert("RGB")


def compose_frame(basemap, radar):
    """JPEG bytes: radar (RGBA, viewport size) over the styled basemap, plus
    a small ring marking the location (its centre left uncovered)."""
    overlay = radar.copy()
    overlay.putalpha(overlay.getchannel("A").point(lambda a: min(255, int(a * RADAR_ALPHA_GAIN))))
    image = Image.alpha_composite(basemap.convert("RGBA"), overlay).convert("RGB")
    draw = ImageDraw.Draw(image)
    cx, cy = image.width // 2, image.height // 2
    draw.ellipse((cx - 5, cy - 5, cx + 5, cy + 5), outline=(0, 0, 0))
    draw.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), outline=(255, 255, 255))
    buffer = io.BytesIO()
    # Baseline JPEG: TJpg_Decoder cannot decode progressive files.
    image.save(buffer, "JPEG", quality=JPEG_QUALITY, optimize=True, progressive=False)
    return buffer.getvalue()


def _open_tile(body):
    try:
        tile = Image.open(io.BytesIO(body))
        tile.load()
    except Exception as error:
        raise RadarError(f"bad tile image: {error}") from error
    if tile.size != (TILE_SIZE, TILE_SIZE):
        raise RadarError(f"bad tile size {tile.size}")
    return tile


# --- Service ----------------------------------------------------------------------------

class RadarService:
    def __init__(self, cache_dir, lat, lon, timezone, fetch=http_fetch, clock=time.time,
                 basemap_url=None):
        self.cache_dir = Path(cache_dir)
        self.lat, self.lon = lat, lon
        self.zone = ZoneInfo(timezone)
        self.fetch = fetch
        self.clock = clock
        self.basemap_url = basemap_url or os.environ.get("RADAR_BASEMAP_URL", DEFAULT_BASEMAP_URL)
        self.left, self.top, self.tiles = viewport(lat, lon)
        # The frame cache belongs to one view; another location/size starts over.
        view = f"{lat:.4f},{lon:.4f},{ZOOM},{FRAME_WIDTH}x{FRAME_HEIGHT},{COLOR_SCHEME},v1"
        self.frames_dir = self.cache_dir / "frames" / format(abs(hash_text(view)), "08x")
        self.lock = threading.Lock()
        self.frames = []          # [(time, Path)], oldest first.
        self.updated = 0          # Last successful refresh.
        self.last_attempt = 0
        self.last_ok = False
        self.refreshing = False
        self.error = None
        self.stats = {}
        self._basemap = None
        self._loaded = False

    # Endpoint side (never blocks on the network).

    def metadata(self):
        self._load_existing()
        self.ensure_fresh()
        now = self.clock()
        with self.lock:
            frames = list(self.frames)
            payload = {"available": bool(frames), "updating": self.refreshing}
            if frames:
                payload.update({
                    "stale": now - self.updated > STALE_SECONDS or not self.last_ok,
                    "updated": int(self.updated),
                    "width": FRAME_WIDTH,
                    "height": FRAME_HEIGHT,
                    "utc_offset": self._utc_offset(frames[-1][0]),
                    "frames": [{"time": stamp, "url": f"/radar/{stamp}.jpg"} for stamp, _ in frames],
                })
            payload["attribution"] = RADAR_ATTRIBUTION
            payload["map_attribution"] = MAP_ATTRIBUTION
        return payload

    def frame_path(self, name):
        """Cached JPEG for a /radar/<name> request, or None."""
        match = _FRAME_NAME_RE.match(name or "")
        if not match:
            return None
        stamp = int(match.group(1))
        with self.lock:
            for frame_time, path in self.frames:
                if frame_time == stamp:
                    return path if path.is_file() else None
        return None

    def ensure_fresh(self):
        """Starts a background refresh when one is due. Returns True if started."""
        now = self.clock()
        with self.lock:
            if self.refreshing:
                return False
            wait = REFRESH_SECONDS if self.last_ok else RETRY_SECONDS
            if self.last_attempt and now - self.last_attempt < wait:
                return False
            self.refreshing = True
        threading.Thread(target=self._refresh_guarded, name="radar-refresh", daemon=True).start()
        return True

    # Refresh side (background thread, or tests directly).

    def refresh(self):
        """One synchronous refresh. Returns True on success."""
        with self.lock:
            self.refreshing = True
        return self._refresh_guarded()

    def _refresh_guarded(self):
        started = self.clock()
        requests = [0]
        try:
            ok = self._refresh(requests)
            error = None
        except Exception as exc:  # Never let a provider problem escape.
            ok, error = False, f"{type(exc).__name__}: {exc}"
        with self.lock:
            self.refreshing = False
            self.last_attempt = self.clock()
            self.last_ok = ok
            self.error = error
            if ok:
                self.updated = self.last_attempt
            self.stats = {"requests": requests[0], "seconds": round(self.clock() - started, 2)}
        return ok

    def _get(self, url, requests, headers=None):
        requests[0] += 1
        return self.fetch(url, headers or {})

    def _refresh(self, requests):
        status, _, body = self._get(RAINVIEWER_MAPS_URL, requests)
        if status != 200:
            raise RadarError(f"metadata HTTP {status}")
        try:
            maps = json.loads(body)
        except ValueError as error:
            raise RadarError("metadata is not JSON") from error
        host, selected = select_frames(maps)

        self.frames_dir.mkdir(parents=True, exist_ok=True)
        basemap = None
        rendered = []
        for stamp, path in selected:
            target = self.frames_dir / f"{stamp}.jpg"
            if not target.is_file():
                if basemap is None:
                    basemap = self._styled_basemap(requests)
                radar = {}
                for tx, ty in self.tiles:
                    url = f"{host}{path}/{TILE_SIZE}/{ZOOM}/{tile_url_x(tx)}/{ty}/{COLOR_SCHEME}/{TILE_OPTIONS}.png"
                    try:
                        tile_status, _, tile_body = self._get(url, requests)
                        if tile_status != 200:
                            raise RadarError(f"radar tile HTTP {tile_status}")
                        radar[(tx, ty)] = _open_tile(tile_body)
                    except Exception:
                        radar = None
                        break
                if radar is None:
                    continue  # Skip this frame; the next refresh retries it.
                data = compose_frame(basemap, mosaic(radar, self.left, self.top))
                write_atomic(target, data)
            rendered.append((stamp, target))

        if not rendered:
            raise RadarError("no frame could be rendered")
        with self.lock:
            self.frames = rendered
        keep = {path.name for _, path in rendered}
        for old in self.frames_dir.glob("*.jpg"):
            if old.name not in keep:
                old.unlink(missing_ok=True)
        return True

    def _styled_basemap(self, requests):
        if self._basemap is None or self._basemap_expired():
            tiles = {key: self._basemap_tile(key, requests) for key in self.tiles}
            self._basemap = style_basemap(mosaic(tiles, self.left, self.top, mode="RGB"))
            self._basemap_checked = self.clock()
        return self._basemap

    def _basemap_expired(self):
        return self.clock() - getattr(self, "_basemap_checked", 0) > BASEMAP_MIN_SECONDS

    def _basemap_tile(self, key, requests):
        """OSM tile from the disk cache; revalidated (conditional GET) once it is
        older than max(7 days, max-age). On failure a cached copy is used."""
        tx, ty = key
        tile_dir = self.cache_dir / "basemap"
        tile_dir.mkdir(parents=True, exist_ok=True)
        tx = tile_url_x(tx)
        image_path = tile_dir / f"{ZOOM}_{tx}_{ty}.png"
        meta_path = tile_dir / f"{ZOOM}_{tx}_{ty}.json"
        meta = {}
        if meta_path.is_file():
            try:
                meta = json.loads(meta_path.read_text())
            except ValueError:
                meta = {}
        now = self.clock()
        if image_path.is_file() and now < meta.get("expires", 0):
            return _open_tile(image_path.read_bytes())

        headers = {}
        if image_path.is_file():
            if meta.get("etag"):
                headers["If-None-Match"] = meta["etag"]
            if meta.get("last_modified"):
                headers["If-Modified-Since"] = meta["last_modified"]
        url = self.basemap_url.format(z=ZOOM, x=tx, y=ty)
        try:
            status, response_headers, body = self._get(url, requests, headers)
            lowered = {k.lower(): v for k, v in response_headers.items()}
            if status == 304 and image_path.is_file():
                tile = _open_tile(image_path.read_bytes())
            elif status == 200:
                tile = _open_tile(body)
                write_atomic(image_path, body)
                meta = {"etag": lowered.get("etag"), "last_modified": lowered.get("last-modified")}
            else:
                raise RadarError(f"basemap HTTP {status}")
            meta["expires"] = now + max(BASEMAP_MIN_SECONDS, max_age(lowered.get("cache-control")))
            write_atomic(meta_path, json.dumps(meta).encode())
            return tile
        except Exception:
            if image_path.is_file():
                return _open_tile(image_path.read_bytes())
            raise

    def _load_existing(self):
        """After a restart, serve frames still on disk (stale until refreshed)."""
        if self._loaded:
            return
        self._loaded = True
        frames = []
        if self.frames_dir.is_dir():
            for path in self.frames_dir.glob("*.jpg"):
                match = _FRAME_NAME_RE.match(path.name)
                if match:
                    frames.append((int(match.group(1)), path))
        frames.sort()
        frames_root = self.cache_dir / "frames"
        if frames_root.is_dir():
            for other in frames_root.iterdir():
                if other.is_dir() and other != self.frames_dir:
                    shutil.rmtree(other, ignore_errors=True)
        with self.lock:
            if not self.frames and frames:
                self.frames = frames[-FRAME_COUNT:]
                self.updated = max(path.stat().st_mtime for _, path in self.frames)

    def _utc_offset(self, stamp):
        offset = datetime.fromtimestamp(stamp, self.zone).utcoffset()
        return int(offset.total_seconds()) if offset else 0


def max_age(cache_control):
    match = re.search(r"max-age=(\d+)", cache_control or "")
    return int(match.group(1)) if match else 0


def hash_text(text):
    """Stable 32-bit FNV-1a (Python's hash() is salted per process)."""
    value = 0x811C9DC5
    for byte in text.encode():
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def write_atomic(path, data):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)

"""Firmware releases for HomeDeck Mini pull updates (disabled by default).

The backend publishes at most one release, described by a small JSON
manifest next to the firmware file:

    {
      "product": "homedeck-mini",
      "version": "1.1.0",
      "filename": "homedeck-mini-1.1.0.bin",
      "size": 1047421,
      "sha256": "<64 hex characters>",
      "notes": "Improved Wi-Fi setup and stability."
    }

The manifest is validated on every request (it is tiny, and the file on disk
is re-hashed only when its size or modification time changes). An invalid
manifest, or a firmware file that does not match it, is never offered or
served.

Trust model: the device trusts the backend it was configured to use. The
SHA-256 protects the download against corruption and truncation; it does not
prove who published the firmware, so anyone who controls this backend (or
its release directory) controls what a device installs. Signed releases are
a later hardening step.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import json
import re

PRODUCT = "homedeck-mini"
UPDATE_PATH = "/api/firmware/update"
DOWNLOAD_PREFIX = "/firmware/"

# Upper bound for any manifest size; the device also checks its own OTA slot.
MAX_FIRMWARE_SIZE = 4 * 1024 * 1024
MAX_NOTES = 200          # The device shows at most this many characters.
MAX_MANIFEST_BYTES = 4096
MAX_VERSION_PART = 65535

_VERSION = re.compile(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})")
# A plain file name: no directories, no leading dot, .bin only.
_FILENAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,59}\.bin")
_SHA256 = re.compile(r"[0-9a-f]{64}")
_MANIFEST_KEYS = {"product", "version", "filename", "size", "sha256", "notes"}


class ManifestError(Exception):
    """The manifest or its firmware file is unusable; nothing is offered."""


@dataclass(frozen=True)
class Release:
    product: str
    version: str
    filename: str
    size: int
    sha256: str
    notes: str
    path: Path   # Never sent to clients.

    @property
    def url(self):
        return DOWNLOAD_PREFIX + self.filename


def parse_version(text):
    """'1.2.3' -> (1, 2, 3); None for anything else (pre-releases, 'v1.2.3',
    leading zeros, more or fewer parts, parts above 65535)."""
    if not isinstance(text, str):
        return None
    match = _VERSION.fullmatch(text)
    if not match:
        return None
    parts = tuple(int(part) for part in match.groups())
    return parts if all(part <= MAX_VERSION_PART for part in parts) else None


def valid_filename(name):
    return isinstance(name, str) and bool(_FILENAME.fullmatch(name)) and ".." not in name


def validate_manifest(data):
    """The manifest fields, checked; raises ManifestError. Unknown keys are
    rejected so a typo cannot silently drop a field."""
    if not isinstance(data, dict):
        raise ManifestError("manifest must be a JSON object")
    unknown = set(data) - _MANIFEST_KEYS
    if unknown:
        raise ManifestError(f"unknown manifest keys: {', '.join(sorted(unknown))}")
    product = data.get("product")
    if product != PRODUCT:
        raise ManifestError(f"product must be {PRODUCT!r}")
    version = data.get("version")
    if parse_version(version) is None:
        raise ManifestError("version must be MAJOR.MINOR.PATCH, e.g. 1.1.0")
    filename = data.get("filename")
    if not valid_filename(filename):
        raise ManifestError("filename must be a plain .bin file name")
    size = data.get("size")
    if type(size) is not int or not 0 < size <= MAX_FIRMWARE_SIZE:
        raise ManifestError(f"size must be a whole number of bytes, 1-{MAX_FIRMWARE_SIZE}")
    sha256 = data.get("sha256")
    if not isinstance(sha256, str) or not _SHA256.fullmatch(sha256.lower()):
        raise ManifestError("sha256 must be 64 hexadecimal characters")
    notes = data.get("notes", "")
    if not isinstance(notes, str) or len(notes) > MAX_NOTES:
        raise ManifestError(f"notes must be text of at most {MAX_NOTES} characters")
    if any(not " " <= ch <= "~" for ch in notes):
        raise ManifestError("notes must be printable ASCII (the display has no other glyphs)")
    return product, version, filename, size, sha256.lower(), notes.strip()


def _file_sha256(path, chunk=65536):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(chunk), b""):
            digest.update(block)
    return digest.hexdigest()


class ReleaseStore:
    """The published release, from FIRMWARE_MANIFEST_FILE and FIRMWARE_RELEASE_DIR."""

    def __init__(self, enabled, release_dir, manifest_file):
        self.enabled = enabled
        self.release_dir = Path(release_dir)
        self.manifest_file = Path(manifest_file)
        self._hash_cache = {}  # (path, size, mtime_ns) -> sha256

    def release(self):
        """The validated release, or None when no manifest exists. Raises
        ManifestError for an invalid manifest or a missing/mismatched file."""
        try:
            raw = self.manifest_file.read_bytes()
        except FileNotFoundError:
            return None
        except OSError as error:
            raise ManifestError(f"cannot read the manifest: {error.strerror or error}") from None
        if len(raw) > MAX_MANIFEST_BYTES:
            raise ManifestError("manifest is too large")
        try:
            data = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            raise ManifestError("manifest is not valid JSON") from None
        product, version, filename, size, sha256, notes = validate_manifest(data)

        root = self.release_dir.resolve()
        path = (root / filename).resolve()
        if path.parent != root:  # Also refuses a symlink that leaves the directory.
            raise ManifestError("firmware file must be inside the release directory")
        try:
            stat = path.stat()
        except OSError:
            raise ManifestError("firmware file is missing") from None
        if not path.is_file():
            raise ManifestError("firmware file is missing")
        if stat.st_size != size:
            raise ManifestError(f"firmware file is {stat.st_size} bytes; the manifest says {size}")
        key = (str(path), stat.st_size, stat.st_mtime_ns)
        if key not in self._hash_cache:
            self._hash_cache = {key: _file_sha256(path)}
        if self._hash_cache[key] != sha256:
            raise ManifestError("firmware file does not match the manifest sha256")
        return Release(product, version, filename, size, sha256, notes, path)

    def check(self, product, current):
        """(HTTP status, JSON payload) for GET /api/firmware/update."""
        if not self.enabled:
            return 404, {"available": False, "error": "firmware updates are disabled"}
        current_version = parse_version(current)
        if not product or current_version is None:
            return 400, {"available": False, "error": "expected product and current=MAJOR.MINOR.PATCH"}
        try:
            release = self.release()
        except ManifestError as error:
            print("Firmware manifest:", error)
            return 503, {"available": False, "error": "firmware release is unavailable"}
        if release is None or product != release.product:
            return 200, {"available": False}
        # Never offer the same or an older version.
        if parse_version(release.version) <= current_version:
            return 200, {"available": False}
        return 200, {
            "available": True,
            "version": release.version,
            "size": release.size,
            "sha256": release.sha256,
            "notes": release.notes,
            "url": release.url,
        }

    def download(self, filename):
        """The Release whose file may be served for /firmware/<filename>, or
        None. Only the current manifest's file is ever served; the name must
        match it exactly (no decoding, no paths)."""
        if not self.enabled or not valid_filename(filename):
            return None
        try:
            release = self.release()
        except ManifestError as error:
            print("Firmware manifest:", error)
            return None
        if release is None or release.filename != filename:
            return None
        return release

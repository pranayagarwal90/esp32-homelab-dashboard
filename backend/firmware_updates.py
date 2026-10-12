"""Firmware releases for HomeDeck Mini pull updates (disabled by default).

The backend publishes at most one release, described by a small signed JSON
manifest (version 2) next to the firmware file:

    {
      "manifest_version": 2,
      "product": "homedeck-mini",
      "version": "1.1.0",
      "filename": "homedeck-mini-1.1.0.bin",
      "size": 1047421,
      "sha256": "<64 lower-case hex characters>",
      "notes": "Improved Wi-Fi setup and stability.",
      "key_id": "homedeck-release-2026",
      "signature": "<base64 of the 64-byte raw ECDSA P-256 r||s>"
    }

The signature is made offline with the project's release key
(tools/prepare-firmware-release.py) over canonical_payload(), never over the
JSON text. The backend is not a trust authority: it checks only the
signature's format, never verifies or creates signatures, and has no key.
HomeDeck Mini verifies it against the public keys built into its firmware
before offering the update.

The manifest is validated on every request (it is tiny, and the file on disk
is re-hashed only when its size or modification time changes). An invalid
manifest, or a firmware file that does not match it, is never offered or
served.

Trust model: the signature proves the release (including its SHA-256) was
published by the holder of a trusted release key; the SHA-256 then proves the
downloaded image is that release. A compromised backend can withhold
updates, but not make a device install firmware the key holder never signed.
"""
import base64
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
MANIFEST_VERSION = 2
SIGNATURE_BYTES = 64     # Raw r||s, each 32 bytes big-endian.

_VERSION = re.compile(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})")
# A plain file name: no directories, no leading dot, .bin only.
_FILENAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,59}\.bin")
_SHA256 = re.compile(r"[0-9a-f]{64}")
_KEY_ID = re.compile(r"[a-z0-9][a-z0-9-]{0,31}")
_MANIFEST_KEYS = {"manifest_version", "product", "version", "filename", "size", "sha256", "notes",
                  "key_id", "signature"}


class ManifestError(Exception):
    """The manifest or its firmware file is unusable; nothing is offered."""


@dataclass(frozen=True)
class Manifest:
    """A structurally valid v2 manifest (signature not verified here)."""
    manifest_version: int
    product: str
    version: str
    filename: str
    size: int
    sha256: str
    notes: str
    key_id: str
    signature: str  # Base64 of 64 bytes, canonical encoding.


@dataclass(frozen=True)
class Release(Manifest):
    path: Path = None  # Never sent to clients.

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


def valid_key_id(key_id):
    return isinstance(key_id, str) and bool(_KEY_ID.fullmatch(key_id))


def decode_signature(text):
    """The 64 raw signature bytes, or None unless text is their canonical
    padded base64 (88 characters)."""
    if not isinstance(text, str) or len(text) != 88:
        return None
    try:
        raw = base64.b64decode(text, validate=True)
    except ValueError:
        return None
    return raw if len(raw) == SIGNATURE_BYTES and base64.b64encode(raw).decode() == text else None


def canonical_payload(manifest_version, product, version, filename, size, sha256, notes, key_id):
    """The exact bytes a release signature covers (ECDSA P-256 over their
    SHA-256). Fixed field order; each field is name=<decimal length>:<value>
    and a line feed, so no value can run into the next; values are ASCII
    (validated). Mirrored by firmware/include/ManifestSignature.h; vectors in
    firmware/test/vectors/manifest_signature_v2.json."""
    fields = (("manifest_version", str(manifest_version)), ("product", product), ("version", version),
              ("filename", filename), ("size", str(size)), ("sha256", sha256.lower()), ("notes", notes),
              ("key_id", key_id))
    out = "HOMEDECK-MANIFEST-SIGNATURE\n"
    for name, value in fields:
        out += f"{name}={len(value)}:{value}\n"
    return out.encode("ascii")


def validate_manifest(data):
    """A Manifest from parsed JSON; raises ManifestError. Unknown keys are
    rejected so a typo cannot silently drop a field. Unsigned (version 1)
    manifests are not accepted."""
    if not isinstance(data, dict):
        raise ManifestError("manifest must be a JSON object")
    unknown = set(data) - _MANIFEST_KEYS
    if unknown:
        raise ManifestError(f"unknown manifest keys: {', '.join(sorted(unknown))}")
    manifest_version = data.get("manifest_version")
    if type(manifest_version) is not int or manifest_version != MANIFEST_VERSION:
        raise ManifestError("manifest_version must be 2 (signed); unsigned manifests are not served")
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
    key_id = data.get("key_id")
    if not valid_key_id(key_id):
        raise ManifestError("key_id must be 1-32 characters: a-z, 0-9 and '-'")
    signature = data.get("signature")
    if decode_signature(signature) is None:
        raise ManifestError("signature must be base64 of a 64-byte ECDSA P-256 r||s signature")
    # Notes are served exactly as signed (never trimmed or changed).
    return Manifest(manifest_version, product, version, filename, size, sha256.lower(), notes, key_id, signature)


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
        manifest = validate_manifest(data)
        filename, size, sha256 = manifest.filename, manifest.size, manifest.sha256

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
        return Release(**manifest.__dict__, path=path)

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
        # Every signed field, unchanged, so the device can rebuild the signed
        # payload; "url" is the download path for these fields' filename.
        return 200, {
            "available": True,
            "manifest_version": release.manifest_version,
            "product": release.product,
            "version": release.version,
            "filename": release.filename,
            "size": release.size,
            "sha256": release.sha256,
            "notes": release.notes,
            "key_id": release.key_id,
            "signature": release.signature,
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

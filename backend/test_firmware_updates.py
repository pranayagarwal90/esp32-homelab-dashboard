"""Firmware update API tests: temporary release directories and a local test
server only; never reads the developer's .env or real release files."""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import tempfile
import threading
import unittest
from contextlib import redirect_stdout
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from unittest.mock import patch

import config
import firmware_updates
from firmware_updates import (ManifestError, ReleaseStore, canonical_payload, decode_signature, parse_version,
                              validate_manifest)

# Hermetic: never read the developer's .env.
with patch.dict(os.environ, {"DASHBOARD_ENV_FILE": ""}), patch("http.server.HTTPServer"), \
        redirect_stdout(io.StringIO()):
    backend = runpy.run_path(str(Path(__file__).with_name("server.py")))
globals_ = backend["Handler"].do_GET.__globals__

FIRMWARE = bytes([0xE9]) + bytes(range(256)) * 40 + b"homedeck"
SHA = hashlib.sha256(FIRMWARE).hexdigest()


# The backend never verifies signatures: an opaque, well-formed one is enough
# here (real signatures: tools/test_release_signing.py).
SIGNATURE = base64.b64encode(bytes(range(64))).decode()


def manifest(**changes):
    data = {"manifest_version": 2, "product": "homedeck-mini", "version": "1.1.0",
            "filename": "homedeck-mini-1.1.0.bin", "size": len(FIRMWARE), "sha256": SHA,
            "notes": "Improved Wi-Fi setup and stability.", "key_id": "homedeck-release-2026",
            "signature": SIGNATURE}
    data.update(changes)
    return {key: value for key, value in data.items() if value is not None}


class ReleaseDir:
    """A temporary release directory with a firmware file and manifest."""

    def __init__(self, data=None, firmware=FIRMWARE, name="homedeck-mini-1.1.0.bin", enabled=True):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name) / "firmware-releases"
        self.dir.mkdir()
        if firmware is not None:
            (self.dir / name).write_bytes(firmware)
        if data is not None:
            text = data if isinstance(data, str) else json.dumps(data)
            (self.dir / "manifest.json").write_text(text)
        self.store = ReleaseStore(enabled, self.dir, self.dir / "manifest.json")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.temp.cleanup()


class VersionTests(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(parse_version("1.0.0"), (1, 0, 0))
        self.assertEqual(parse_version("2.10.3"), (2, 10, 3))
        self.assertEqual(parse_version("65535.0.0"), (65535, 0, 0))

    def test_malformed(self):
        for text in ["", "1", "1.0", "1.0.0.0", "v1.0.0", "1.0.0-rc1", "1.0.0+build", "01.0.0",
                     "1..0", " 1.0.0", "1.0.0 ", "1.0.a", "65536.0.0", "١.0.0", None, 100]:
            self.assertIsNone(parse_version(text), text)

    def test_ordering(self):
        order = ["1.0.0", "1.0.1", "1.1.0", "1.10.0", "2.0.0"]
        parsed = [parse_version(text) for text in order]
        self.assertEqual(parsed, sorted(parsed))


class ManifestValidationTests(unittest.TestCase):
    def test_valid_manifest(self):
        m = validate_manifest(manifest(sha256=SHA.upper()))
        self.assertEqual((m.manifest_version, m.product, m.version, m.filename, m.size, m.sha256, m.notes,
                          m.key_id, m.signature),
                         (2, "homedeck-mini", "1.1.0", "homedeck-mini-1.1.0.bin", len(FIRMWARE), SHA,
                          "Improved Wi-Fi setup and stability.", "homedeck-release-2026", SIGNATURE))

    def test_notes_are_optional_and_never_altered(self):
        self.assertEqual(validate_manifest(manifest(notes=None)).notes, "")
        # Signed exactly as written: never trimmed.
        self.assertEqual(validate_manifest(manifest(notes=" padded ")).notes, " padded ")

    def test_unsigned_and_signature_format(self):
        sig = bytes(range(64))
        cases = {
            "version 1 (unsigned)": manifest(manifest_version=1, key_id=None, signature=None),
            "no manifest_version": manifest(manifest_version=None),
            "manifest_version 3": manifest(manifest_version=3),
            "manifest_version string": manifest(manifest_version="2"),
            "manifest_version bool": manifest(manifest_version=True),
            "missing key_id": manifest(key_id=None),
            "bad key_id": manifest(key_id="Release 2026"),
            "long key_id": manifest(key_id="k" * 33),
            "missing signature": manifest(signature=None),
            "invalid base64": manifest(signature="!" * 88),
            "truncated": manifest(signature=base64.b64encode(sig[:63]).decode()),
            "too long": manifest(signature=base64.b64encode(sig + b"x").decode()),
            "unpadded": manifest(signature=base64.b64encode(sig).decode().rstrip("=")),
            "whitespace": manifest(signature=" " + base64.b64encode(sig).decode()),
            "url-safe alphabet": manifest(signature=base64.urlsafe_b64encode(bytes([0xFB] * 64)).decode()),
        }
        for name, data in cases.items():
            with self.subTest(name), self.assertRaises(ManifestError):
                validate_manifest(data)
        self.assertEqual(decode_signature(SIGNATURE), sig)
        # Non-canonical trailing bits are refused: one signature, one spelling.
        self.assertIsNone(decode_signature(SIGNATURE[:85] + "B=="))

    def test_canonical_payload_format(self):
        payload = canonical_payload(2, "homedeck-mini", "1.0.2", "homedeck-mini-1.0.2.bin", 1069000, "AB" * 32,
                                    "Improved reliability.", "homedeck-release-2026")
        self.assertEqual(payload, (b"HOMEDECK-MANIFEST-SIGNATURE\nmanifest_version=1:2\nproduct=13:homedeck-mini\n"
                                   b"version=5:1.0.2\nfilename=23:homedeck-mini-1.0.2.bin\nsize=7:1069000\n"
                                   b"sha256=64:" + b"ab" * 32 + b"\nnotes=21:Improved reliability.\n"
                                   b"key_id=21:homedeck-release-2026\n"))

    def test_rejected(self):
        cases = {
            "not an object": [],
            "product": manifest(product="homelab-display"),
            "invalid version": manifest(version="1.1"),
            "prerelease": manifest(version="1.1.0-beta"),
            "missing filename": manifest(filename=None),
            "traversal": manifest(filename="../secrets.bin"),
            "absolute": manifest(filename="/etc/passwd.bin"),
            "subdirectory": manifest(filename="sub/firmware.bin"),
            "backslash": manifest(filename="..\\firmware.bin"),
            "hidden": manifest(filename=".firmware.bin"),
            "not .bin": manifest(filename="firmware.elf"),
            "dot dot inside": manifest(filename="a..bin"),
            "zero size": manifest(size=0),
            "negative size": manifest(size=-1),
            "string size": manifest(size="1000"),
            "bool size": manifest(size=True),
            "float size": manifest(size=1000.0),
            "huge size": manifest(size=firmware_updates.MAX_FIRMWARE_SIZE + 1),
            "short sha": manifest(sha256=SHA[:63]),
            "non-hex sha": manifest(sha256="g" * 64),
            "missing sha": manifest(sha256=None),
            "long notes": manifest(notes="x" * 201),
            "non-ascii notes": manifest(notes="Café"),
            "control notes": manifest(notes="line\nbreak"),
            "unknown key": dict(manifest(), url="http://elsewhere/firmware.bin"),
        }
        for name, data in cases.items():
            with self.subTest(name), self.assertRaises(ManifestError):
                validate_manifest(data)


class ReleaseStoreTests(unittest.TestCase):
    def test_updates_disabled(self):
        with ReleaseDir(manifest(), enabled=False) as rel:
            self.assertEqual(rel.store.check("homedeck-mini", "1.0.0")[0], 404)
            self.assertIsNone(rel.store.download("homedeck-mini-1.1.0.bin"))

    def test_disabled_by_default(self):
        cfg = config.load(environ={}, env_file="")
        self.assertFalse(cfg.firmware_updates_enabled)
        self.assertEqual(cfg.firmware_release_dir, config.PROJECT_ROOT / "firmware-releases")
        self.assertEqual(cfg.firmware_manifest_file, config.PROJECT_ROOT / "firmware-releases" / "manifest.json")

    def test_config_values(self):
        cfg = config.load(environ={"FIRMWARE_UPDATES_ENABLED": "true", "FIRMWARE_RELEASE_DIR": "/srv/fw"},
                          env_file="")
        self.assertTrue(cfg.firmware_updates_enabled)
        self.assertEqual(cfg.firmware_manifest_file, Path("/srv/fw/manifest.json"))
        cfg = config.load(environ={"FIRMWARE_MANIFEST_FILE": "/srv/other.json"}, env_file="")
        self.assertEqual(cfg.firmware_manifest_file, Path("/srv/other.json"))
        with self.assertRaises(config.ConfigError):
            config.load(environ={"FIRMWARE_UPDATES_ENABLED": "maybe"}, env_file="")

    def test_manifest_missing(self):
        with ReleaseDir(None) as rel:
            self.assertEqual(rel.store.check("homedeck-mini", "1.0.0"), (200, {"available": False}))
            self.assertIsNone(rel.store.download("homedeck-mini-1.1.0.bin"))

    def test_newer_version(self):
        with ReleaseDir(manifest()) as rel:
            status, payload = rel.store.check("homedeck-mini", "1.0.0")
            self.assertEqual(status, 200)
            # Every signed field, unchanged, plus the download path.
            self.assertEqual(payload, {"available": True, "manifest_version": 2, "product": "homedeck-mini",
                                       "version": "1.1.0", "filename": "homedeck-mini-1.1.0.bin",
                                       "size": len(FIRMWARE), "sha256": SHA,
                                       "notes": "Improved Wi-Fi setup and stability.",
                                       "key_id": "homedeck-release-2026", "signature": SIGNATURE,
                                       "url": "/firmware/homedeck-mini-1.1.0.bin"})
            self.assertNotIn(str(rel.dir), json.dumps(payload))  # No filesystem paths.

    def test_same_and_older_versions(self):
        with ReleaseDir(manifest()) as rel:
            for current in ["1.1.0", "1.1.1", "1.2.0", "2.0.0"]:
                self.assertEqual(rel.store.check("homedeck-mini", current), (200, {"available": False}), current)
            self.assertTrue(rel.store.check("homedeck-mini", "1.0.9")[1]["available"])

    def test_numeric_not_text_comparison(self):
        with ReleaseDir(manifest(version="1.10.0")) as rel:
            self.assertTrue(rel.store.check("homedeck-mini", "1.9.0")[1]["available"])

    def test_bad_query(self):
        with ReleaseDir(manifest()) as rel:
            for product, current in [("homedeck-mini", ""), ("homedeck-mini", "1.0"), ("", "1.0.0"),
                                     ("homedeck-mini", "1.0.0-rc1")]:
                self.assertEqual(rel.store.check(product, current)[0], 400)

    def test_other_product(self):
        with ReleaseDir(manifest()) as rel:
            self.assertEqual(rel.store.check("other-device", "1.0.0"), (200, {"available": False}))

    def test_invalid_manifests_are_never_offered(self):
        cases = {
            "malformed json": "{not json",
            "malformed version": manifest(version="1.1"),
            "invalid sha": manifest(sha256="0" * 63),
            "invalid size": manifest(size=-5),
            "traversal": manifest(filename="../homedeck-mini-1.1.0.bin"),
            "size mismatch": manifest(size=len(FIRMWARE) + 1),
            "sha mismatch": manifest(sha256="0" * 64),
        }
        for name, data in cases.items():
            with self.subTest(name), ReleaseDir(data) as rel, redirect_stdout(io.StringIO()):
                self.assertEqual(rel.store.check("homedeck-mini", "1.0.0")[0], 503)
                self.assertIsNone(rel.store.download("homedeck-mini-1.1.0.bin"))

    def test_firmware_missing(self):
        with ReleaseDir(manifest(), firmware=None) as rel, redirect_stdout(io.StringIO()):
            self.assertEqual(rel.store.check("homedeck-mini", "1.0.0")[0], 503)
            self.assertIsNone(rel.store.download("homedeck-mini-1.1.0.bin"))

    def test_symlink_out_of_directory_refused(self):
        with ReleaseDir(manifest(), firmware=None) as rel, redirect_stdout(io.StringIO()):
            outside = Path(rel.temp.name) / "outside.bin"
            outside.write_bytes(FIRMWARE)
            (rel.dir / "homedeck-mini-1.1.0.bin").symlink_to(outside)
            self.assertEqual(rel.store.check("homedeck-mini", "1.0.0")[0], 503)

    def test_only_manifest_file_downloadable(self):
        with ReleaseDir(manifest()) as rel:
            (rel.dir / "other.bin").write_bytes(FIRMWARE)
            self.assertIsNotNone(rel.store.download("homedeck-mini-1.1.0.bin"))
            for name in ["other.bin", "manifest.json", "../homedeck-mini-1.1.0.bin", "",
                         "homedeck-mini-1.1.0.bin?x=1", "%2e%2e%2fmanifest.json"]:
                self.assertIsNone(rel.store.download(name), name)

    def test_file_replaced_after_check_is_rehashed(self):
        with ReleaseDir(manifest()) as rel, redirect_stdout(io.StringIO()):
            self.assertEqual(rel.store.check("homedeck-mini", "1.0.0")[0], 200)
            corrupted = bytearray(FIRMWARE)
            corrupted[10] ^= 0xFF
            path = rel.dir / "homedeck-mini-1.1.0.bin"
            path.write_bytes(bytes(corrupted))
            os.utime(path, ns=(1, 1))  # Same size; a different modification time.
            self.assertEqual(rel.store.check("homedeck-mini", "1.0.0")[0], 503)


class HttpTests(unittest.TestCase):
    """The real Handler on a local port."""

    def serve(self, store):
        server = ThreadingHTTPServer(("127.0.0.1", 0), backend["Handler"])
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        patcher = patch.dict(globals_, {"RELEASES": store})
        patcher.start()
        thread.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return server.server_address[1]

    def get(self, port, path):
        connection = HTTPConnection("127.0.0.1", port, timeout=5)
        connection.request("GET", path)
        response = connection.getresponse()
        body = response.read()
        connection.close()
        return response, body

    def test_update_endpoint(self):
        with ReleaseDir(manifest()) as rel:
            port = self.serve(rel.store)
            response, body = self.get(port, "/api/firmware/update?product=homedeck-mini&current=1.0.0")
            self.assertEqual(response.status, 200)
            self.assertEqual(response.getheader("Content-Type"), "application/json")
            payload = json.loads(body)
            self.assertTrue(payload["available"])
            self.assertEqual(payload["url"], "/firmware/homedeck-mini-1.1.0.bin")
            response, body = self.get(port, "/api/firmware/update?product=homedeck-mini&current=1.1.0")
            self.assertEqual(json.loads(body), {"available": False})
            response, _ = self.get(port, "/api/firmware/update?current=1.0.0")
            self.assertEqual(response.status, 400)

    def test_disabled_endpoint_is_404(self):
        with ReleaseDir(manifest(), enabled=False) as rel:
            port = self.serve(rel.store)
            response, _ = self.get(port, "/api/firmware/update?product=homedeck-mini&current=1.0.0")
            self.assertEqual(response.status, 404)
            response, _ = self.get(port, "/firmware/homedeck-mini-1.1.0.bin")
            self.assertEqual(response.status, 404)

    def test_valid_download_with_content_length(self):
        with ReleaseDir(manifest()) as rel:
            port = self.serve(rel.store)
            response, body = self.get(port, "/firmware/homedeck-mini-1.1.0.bin")
            self.assertEqual(response.status, 200)
            self.assertEqual(response.getheader("Content-Type"), "application/octet-stream")
            self.assertEqual(response.getheader("Content-Length"), str(len(FIRMWARE)))
            self.assertEqual(body, FIRMWARE)
            self.assertEqual(hashlib.sha256(body).hexdigest(), SHA)

    def test_download_refusals(self):
        with ReleaseDir(manifest()) as rel:
            (rel.dir / "other.bin").write_bytes(FIRMWARE)
            port = self.serve(rel.store)
            for path in ["/firmware/", "/firmware/other.bin", "/firmware/manifest.json",
                         "/firmware/../server.py", "/firmware/%2e%2e/server.py",
                         "/firmware/..%2fserver.py", "/firmware/homedeck-mini-1.1.0.bin/",
                         "/firmware/homedeck-mini-1.1.0.bin?x"]:
                response, _ = self.get(port, path)
                self.assertEqual(response.status, 404, path)

    def test_missing_firmware_download_is_404(self):
        with ReleaseDir(manifest(), firmware=None) as rel, redirect_stdout(io.StringIO()):
            port = self.serve(rel.store)
            response, _ = self.get(port, "/firmware/homedeck-mini-1.1.0.bin")
            self.assertEqual(response.status, 404)

    def test_existing_routes_unchanged(self):
        with ReleaseDir(None) as rel:
            port = self.serve(rel.store)
            response, _ = self.get(port, "/api/unknown")
            self.assertEqual(response.status, 404)


class GenericStartupTests(unittest.TestCase):
    def test_server_module_loads_without_release_files(self):
        with tempfile.TemporaryDirectory() as directory:
            environ = {"DASHBOARD_ENV_FILE": "", "FIRMWARE_UPDATES_ENABLED": "true",
                       "FIRMWARE_RELEASE_DIR": str(Path(directory) / "none")}
            with patch.dict(os.environ, environ), patch("http.server.HTTPServer"), \
                    redirect_stdout(io.StringIO()):
                module = runpy.run_path(str(Path(__file__).with_name("server.py")))
            self.assertEqual(module["RELEASES"].check("homedeck-mini", "1.0.0"), (200, {"available": False}))


if __name__ == "__main__":
    unittest.main()

"""Release signing with real ECDSA P-256 (the openssl command): the committed
TEST vectors, tampering, key rotation, the release and key tools, and a scan
for private keys. Every key made here is a throwaway in a temporary
directory. Run: python3 -m unittest discover -s tools -p 'test_*.py'"""
import base64
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))
sys.path.insert(0, str(ROOT / "tools"))
import firmware_updates as fu  # noqa: E402
import release_signing as rs  # noqa: E402

VECTORS = json.loads((ROOT / "firmware/test/vectors/manifest_signature_v2.json").read_text())
FIELDS = ("manifest_version", "product", "version", "filename", "size", "sha256", "notes", "key_id")


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def payload_of(manifest):
    return fu.canonical_payload(*(manifest[field] for field in FIELDS))


def device_check(manifest, trusted):
    """What the firmware decides (ManifestSignature.h checkManifestSignature),
    with real ECDSA: 'verified' or the reason it is refused."""
    if manifest.get("manifest_version") is None or "key_id" not in manifest or "signature" not in manifest:
        return "unsigned"
    if manifest["manifest_version"] != fu.MANIFEST_VERSION:
        return "unsupported"
    signature = fu.decode_signature(manifest["signature"])
    if not fu.valid_key_id(manifest["key_id"]) or signature is None:
        return "bad encoding"
    if manifest["key_id"] not in trusted:
        return "unknown key"
    return "verified" if rs.verify(trusted[manifest["key_id"]], payload_of(manifest), signature) else "bad signature"


TEST_KEYS = {key_id: bytes.fromhex(hex_key) for key_id, hex_key in VECTORS["public_keys"].items()}


class VectorTests(unittest.TestCase):
    def test_marked_test_only(self):
        self.assertIn("TEST ONLY - NOT A PRODUCTION RELEASE KEY", VECTORS["warning"])
        kat = (ROOT / "firmware/include/ManifestSignatureKat.h").read_text()
        self.assertIn("TEST ONLY - NOT A PRODUCTION RELEASE KEY", kat)

    def test_vectors_verify_with_openssl(self):
        for vector in VECTORS["vectors"]:
            manifest = vector["manifest"]
            with self.subTest(vector["name"]):
                fu.validate_manifest(manifest)  # The backend serves them.
                self.assertEqual(payload_of(manifest).hex(), vector["payload_hex"])
                self.assertEqual(hashlib.sha256(payload_of(manifest)).hexdigest(), vector["payload_sha256"])
                self.assertEqual(device_check(manifest, TEST_KEYS), "verified")

    def test_every_field_is_authenticated(self):
        original = VECTORS["vectors"][0]["manifest"]
        changes = {"product": "homedeck-maxi", "version": "1.0.3", "filename": "homedeck-mini-evil.bin",
                   "size": original["size"] + 1, "sha256": "0" + original["sha256"][1:],
                   "notes": original["notes"] + " ", "key_id": "homedeck-test-b", "manifest_version": 3}
        self.assertEqual(set(changes), set(FIELDS))
        for field, value in changes.items():
            with self.subTest(field):
                tampered = dict(original, **{field: value})
                self.assertNotEqual(device_check(tampered, TEST_KEYS), "verified")
                # Even re-signing-free "same payload" tricks fail: the payload changed.
                if field != "manifest_version":
                    self.assertFalse(rs.verify(TEST_KEYS["homedeck-test-a"], payload_of(tampered),
                                               fu.decode_signature(original["signature"])))

    def test_signature_damage(self):
        manifest = VECTORS["vectors"][0]["manifest"]
        raw = fu.decode_signature(manifest["signature"])
        for bit in (0, 100, 255, 256, 400, 511):
            flipped = bytearray(raw)
            flipped[bit // 8] ^= 1 << (bit % 8)
            self.assertEqual(device_check(dict(manifest, signature=base64.b64encode(flipped).decode()), TEST_KEYS),
                             "bad signature")
        self.assertEqual(device_check(dict(manifest, signature=manifest["signature"][:84]), TEST_KEYS), "bad encoding")
        self.assertEqual(device_check(dict(manifest, signature="%" * 88), TEST_KEYS), "bad encoding")
        self.assertEqual(device_check(dict(manifest, key_id="homedeck-test-c"), TEST_KEYS), "unknown key")
        unsigned = {k: v for k, v in manifest.items() if k not in ("key_id", "signature")}
        self.assertEqual(device_check(unsigned, TEST_KEYS), "unsigned")


class EncodingTests(unittest.TestCase):
    def test_der_round_trip(self):
        for raw in (bytes(range(64)), bytes([0x80]) * 64, bytes(31) + b"\x01" + bytes(31) + b"\xff",
                    bytes([0xFF]) * 64):
            self.assertEqual(rs.der_to_raw(rs.raw_to_der(raw)), raw)
        self.assertEqual(rs.raw_to_der(bytes([0x80]) * 64)[2:5], b"\x02\x21\x00")  # Positive INTEGER.

    def test_malformed_der_refused(self):
        good = rs.raw_to_der(bytes(range(1, 65)))
        for bad in (b"", good[:-1], good + b"\x00", b"\x31" + good[1:], good[:2] + b"\x03" + good[3:],
                    b"\x30\x06\x02\x01\x01\x02\x22" + bytes(34)):
            with self.assertRaises(rs.SigningError):
                rs.der_to_raw(bad)

    def test_spki_must_be_p256(self):
        point = b"\x04" + bytes(64)
        self.assertEqual(rs.public_key_from_spki(rs.P256_SPKI_PREFIX + point), point)
        with self.assertRaises(rs.SigningError):
            rs.public_key_from_spki(rs.P256_SPKI_PREFIX + b"\x02" + bytes(64))
        with self.assertRaises(rs.SigningError):
            rs.public_key_from_spki(b"\x30" + rs.P256_SPKI_PREFIX[1:] + point + b"\x00")


class KeyTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.dir = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)

    def key(self, name):
        path = self.dir / f"{name}.pem"
        rs.generate_private_key(path, encrypt=False)
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        return path, rs.public_key_from_private(path)

    def signed(self, key_path, key_id, version="1.2.0"):
        manifest = {"manifest_version": 2, "product": "homedeck-mini", "version": version,
                    "filename": f"homedeck-mini-{version}.bin", "size": 1000,
                    "sha256": hashlib.sha256(version.encode()).hexdigest(), "notes": "Rotation test.",
                    "key_id": key_id}
        manifest["signature"] = rs.b64(rs.sign(key_path, payload_of(manifest)))
        return manifest

    def test_rotation(self):
        (a, pub_a), (b, pub_b), (c, _) = self.key("a"), self.key("b"), self.key("c")
        by_a, by_b, by_c = self.signed(a, "key-a"), self.signed(b, "key-b"), self.signed(c, "key-c")
        a_claiming_b = self.signed(a, "key-b")
        keysets = {"[A]": {"key-a": pub_a}, "[A,B]": {"key-a": pub_a, "key-b": pub_b}, "[B]": {"key-b": pub_b}}
        expected = {
            "[A]": ("verified", "unknown key"),
            "[A,B]": ("verified", "verified"),
            "[B]": ("unknown key", "verified"),
        }
        for name, trusted in keysets.items():
            with self.subTest(name):
                self.assertEqual((device_check(by_a, trusted), device_check(by_b, trusted)), expected[name])
                self.assertEqual(device_check(by_c, trusted), "unknown key")
                self.assertNotEqual(device_check(a_claiming_b, trusted), "verified")
        # Same key id, different key (an impostor key named key-a): refused.
        impostor = self.signed(c, "key-a")
        self.assertEqual(device_check(impostor, keysets["[A]"]), "bad signature")

    def test_wrong_key_types_refused(self):
        for name, args in (("p384", ["-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384"]),
                           ("rsa", ["-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"]),
                           ("ed25519", ["-algorithm", "ED25519"])):
            path = self.dir / f"{name}.pem"
            self.assertEqual(rs._openssl(["genpkey", *args, "-out", str(path)]).returncode, 0)
            with self.subTest(name), self.assertRaises(rs.SigningError):
                rs.public_key_from_private(path)
        with self.assertRaises(rs.SigningError):
            rs.public_key_from_private(self.dir / "missing.pem")

    def test_generate_release_key_tool(self):
        tool = load_tool("generate-release-key")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(tool.main(["--key-id", "homedeck-release-test", "--out", str(self.dir / "k.pem"),
                                        "--public-out", str(self.dir / "k.h"), "--no-encrypt"]), 0)
        text = out.getvalue()
        self.assertIn("static const uint8_t RELEASE_KEY_HOMEDECK_RELEASE_TEST[65]", text)
        self.assertNotRegex(text, PEM_PRIVATE)
        self.assertNotIn((self.dir / "k.pem").read_text().splitlines()[1], text)  # Never the key body.
        self.assertEqual((self.dir / "k.pem").stat().st_mode & 0o777, 0o600)
        # Never overwrites; refuses repository paths outside private/; bad ids.
        for argv in (["--key-id", "homedeck-release-test", "--out", str(self.dir / "k.pem"), "--no-encrypt"],
                     ["--key-id", "x", "--out", str(ROOT / "firmware/include/x.pem"), "--no-encrypt"],
                     ["--key-id", "x", "--out", str(ROOT / "backend/x.pem"), "--no-encrypt"],
                     ["--key-id", "Bad Id", "--out", str(self.dir / "n.pem"), "--no-encrypt"]):
            with self.subTest(argv[3]), self.assertRaises(SystemExit), \
                    contextlib.redirect_stderr(io.StringIO()):
                tool.main(argv)
        self.assertFalse((ROOT / "firmware/include/x.pem").exists())
        self.assertFalse((ROOT / "backend/x.pem").exists())

    def firmware_image(self, version):
        data = bytes([0xE9]) + bytes(4096) + f"HOMEDECK-FIRMWARE:homedeck-mini:{version}\0".encode() + bytes(512)
        path = self.dir / "firmware.bin"
        path.write_bytes(data)
        return path, data

    def test_prepare_release_tool_signs_and_self_verifies(self):
        tool = load_tool("prepare-firmware-release")
        key, public = self.key("release")
        image, data = self.firmware_image("1.2.3")
        out_dir = self.dir / "releases"
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.main(["--firmware", str(image), "--version", "1.2.3", "--notes", "  Signed. ",
                                        "--signing-key", str(key), "--key-id", "homedeck-release-test",
                                        "--out-dir", str(out_dir)]), 0)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["manifest_version"], 2)
        # World-readable, like the image: the backend may run as another user.
        self.assertEqual((out_dir / "manifest.json").stat().st_mode & 0o777, 0o644)
        self.assertEqual((manifest["key_id"], manifest["notes"], manifest["size"]),
                         ("homedeck-release-test", "Signed.", len(data)))
        self.assertEqual(manifest["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(device_check(manifest, {"homedeck-release-test": public}), "verified")
        self.assertNotIn(str(key), (out_dir / "manifest.json").read_text())
        # The backend serves it as-is.
        store = fu.ReleaseStore(True, out_dir, out_dir / "manifest.json")
        status, offer = store.check("homedeck-mini", "1.0.0")
        self.assertEqual((status, offer["signature"], offer["key_id"]), (200, manifest["signature"],
                                                                       "homedeck-release-test"))

    def test_prepare_release_tool_fails_closed(self):
        tool = load_tool("prepare-firmware-release")
        image, _ = self.firmware_image("1.2.3")
        p384 = self.dir / "p384.pem"
        rs._openssl(["genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-384", "-out", str(p384)])
        good, _ = self.key("good")
        cases = {
            "wrong curve": ["--signing-key", str(p384), "--key-id", "homedeck-release-test"],
            "missing key": ["--signing-key", str(self.dir / "none.pem"), "--key-id", "homedeck-release-test"],
            "bad key id": ["--signing-key", str(good), "--key-id", "Release_2026"],
            "no key": ["--key-id", "homedeck-release-test"],
        }
        for name, extra in cases.items():
            out_dir = self.dir / name.replace(" ", "-")
            with self.subTest(name), self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()), \
                    contextlib.redirect_stdout(io.StringIO()):
                tool.main(["--firmware", str(image), "--version", "1.2.3", "--out-dir", str(out_dir), *extra])
            self.assertFalse(out_dir.exists(), name)  # Nothing written.


# -----BEGIN ... PRIVATE KEY----- markers, built so this file does not contain one.
PEM_PRIVATE = re.compile("-----BEGIN [A-Z ]*" + "PRIVATE" + " KEY-----")
SKIP_DIRS = {".git", ".pio", ".venv", ".venv-platformio", "venv", "node_modules", "__pycache__",
             "photos-source", "photos-ready", "firmware-releases", "private"}


class SecretScanTests(unittest.TestCase):
    def test_no_private_keys_in_working_tree(self):
        found = []
        for directory, dirs, files in os.walk(ROOT):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for name in files:
                path = Path(directory) / name
                try:
                    if path.stat().st_size > 2_000_000:
                        continue
                    if PEM_PRIVATE.search(path.read_text(errors="ignore")):
                        found.append(str(path.relative_to(ROOT)))
                except OSError:
                    continue
        self.assertEqual(found, [])

    def test_no_private_key_files_tracked_or_unignored(self):
        self.assertIn("private/", (ROOT / ".gitignore").read_text().split())
        for pattern in ("*.pem", "*.key"):
            self.assertIn(pattern, (ROOT / ".gitignore").read_text().split())


if __name__ == "__main__":
    unittest.main()

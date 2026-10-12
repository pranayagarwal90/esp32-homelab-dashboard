#!/usr/bin/env python3
"""Prepare a signed HomeDeck Mini firmware release for the dashboard backend.

Copies a built firmware.bin into the release directory as
homedeck-mini-<version>.bin and writes a signed manifest.json (version 2:
size, SHA-256, notes, key id, ECDSA P-256 signature) next to it. It never
changes the source version, builds, or contacts a device.

    python3 tools/prepare-firmware-release.py \\
        --firmware firmware/.pio/build/esp32dev/firmware.bin \\
        --version 1.1.0 --notes "HomeDeck Mini update" \\
        --signing-key /path/outside/the/repo/homedeck-release-2026.pem \\
        --key-id homedeck-release-2026

The signing key is used only by openssl (by path; a passphrase is asked for
on the terminal) and never copied. The tool verifies its own signature with
the key's public half before writing anything, and fails closed otherwise.
Devices accept the release only if --key-id names a public key built into
their firmware (firmware/include/TrustedUpdateKeys.h).

The version must match the one compiled into the image (BuildInfo.h); the
tool reads it from the image's HOMEDECK-FIRMWARE marker and refuses a
mismatch, so a device can never be offered a release that reports another
version after installing it. The backend serves the release only with
FIRMWARE_UPDATES_ENABLED=true (see .env.example).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))
from firmware_updates import (MANIFEST_VERSION, MAX_FIRMWARE_SIZE, PRODUCT, ManifestError,  # noqa: E402
                              canonical_payload, parse_version, valid_key_id, validate_manifest)
sys.path.insert(0, str(ROOT / "tools"))
import release_signing  # noqa: E402

MARKER_PREFIX = b"HOMEDECK-FIRMWARE:"  # FIRMWARE_ID_PREFIX in firmware/include/BuildInfo.h.
ESP_IMAGE_MAGIC = 0xE9


def image_version(data):
    """The version compiled into the image, or None."""
    match = re.search(re.escape(MARKER_PREFIX + PRODUCT.encode() + b":") + rb"([0-9.]{5,17})\x00", data)
    return match.group(1).decode() if match else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--firmware", required=True, type=Path, help="built firmware.bin")
    parser.add_argument("--version", required=True, help="release version, MAJOR.MINOR.PATCH")
    parser.add_argument("--notes", default="", help="short release notes (printable ASCII, max 200)")
    parser.add_argument("--signing-key", required=True, type=Path,
                        help="release private key (PEM, EC P-256); keep it outside the repository")
    parser.add_argument("--key-id", required=True, help="the key's id in TrustedUpdateKeys.h")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "firmware-releases",
                        help="release directory (default: firmware-releases/ in the project root)")
    args = parser.parse_args(argv)

    if not valid_key_id(args.key_id):
        parser.error(f"--key-id {args.key_id!r}: 1-32 characters, a-z, 0-9 and '-'")
    if parse_version(args.version) is None:
        parser.error(f"--version {args.version!r}: expected MAJOR.MINOR.PATCH, e.g. 1.1.0")
    try:
        data = args.firmware.read_bytes()
    except OSError as error:
        parser.error(f"--firmware: {error}")
    if not data or data[0] != ESP_IMAGE_MAGIC:
        parser.error(f"{args.firmware} is not an ESP32 application image")
    if len(data) > MAX_FIRMWARE_SIZE:
        parser.error(f"{args.firmware} is {len(data)} bytes; the limit is {MAX_FIRMWARE_SIZE}")
    built = image_version(data)
    if built is None:
        parser.error(f"{args.firmware} has no HomeDeck Mini version marker (built from an older source?)")
    if built != args.version:
        parser.error(f"{args.firmware} reports version {built}, not {args.version}; "
                     "bump DASHBOARD_FIRMWARE_VERSION in firmware/include/BuildInfo.h and rebuild")

    filename = f"{PRODUCT}-{args.version}.bin"
    notes = args.notes.strip()
    digest = hashlib.sha256(data).hexdigest()
    payload = canonical_payload(MANIFEST_VERSION, PRODUCT, args.version, filename, len(data), digest, notes,
                                args.key_id)
    unsigned = {"manifest_version": MANIFEST_VERSION, "product": PRODUCT, "version": args.version,
                "filename": filename, "size": len(data), "sha256": digest, "notes": notes,
                "key_id": args.key_id, "signature": release_signing.b64(bytes(64))}
    try:
        validate_manifest(unsigned)  # Everything but the signature, before asking for the key.
    except ManifestError as error:
        parser.error(str(error))
    try:
        public_key = release_signing.public_key_from_private(args.signing_key)  # Type and curve check.
        signature = release_signing.sign(args.signing_key, payload)
        if not release_signing.verify(public_key, payload, signature):
            raise release_signing.SigningError("the new signature does not verify")
    except release_signing.SigningError as error:
        parser.error(f"signing failed: {error}")
    manifest = {"manifest_version": MANIFEST_VERSION, "product": PRODUCT, "version": args.version,
                "filename": filename, "size": len(data), "sha256": digest, "notes": notes,
                "key_id": args.key_id, "signature": release_signing.b64(signature)}
    try:
        validate_manifest(manifest)
    except ManifestError as error:
        parser.error(str(error))

    args.out_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.firmware, args.out_dir / filename)
    # Written atomically, after the firmware file, so the backend never sees
    # a manifest that points at a missing or partial file.
    with tempfile.NamedTemporaryFile("w", dir=args.out_dir, suffix=".tmp", delete=False) as tmp:
        json.dump(manifest, tmp, indent=2)
        tmp.write("\n")
    # Readable by the backend's own user (e.g. the container's), like the .bin.
    os.chmod(tmp.name, 0o644)
    os.replace(tmp.name, args.out_dir / "manifest.json")

    print(f"Release {args.version}: {args.out_dir / filename}")
    print(f"  size    {manifest['size']} bytes")
    print(f"  sha256  {manifest['sha256']}")
    print(f"  notes   {manifest['notes'] or '(none)'}")
    print(f"  signed  {args.key_id} (ECDSA P-256; verified with public key {public_key.hex()[:16]}...)")
    print(f"Manifest: {args.out_dir / 'manifest.json'}")
    print("Serve it with FIRMWARE_UPDATES_ENABLED=true (and matching FIRMWARE_RELEASE_DIR).")
    return 0


if __name__ == "__main__":
    sys.exit(main())

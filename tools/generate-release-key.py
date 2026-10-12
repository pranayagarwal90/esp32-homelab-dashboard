#!/usr/bin/env python3
"""Generate a HomeDeck Mini release signing key (EC P-256) and print the
PUBLIC key in the form firmware/include/TrustedUpdateKeys.h needs.

    python3 tools/generate-release-key.py --key-id homedeck-release-2026 \\
        --out /secure/offline/location/homedeck-release-2026.pem

PRIVATE: --out, written by openssl with mode 0600 and, by default, encrypted
with a passphrase openssl asks for. It is never printed, copied or uploaded.
Inside this repository it is refused anywhere but the git-ignored private/
(other folders are synced or built, e.g. by deploy-esp32.sh); outside the
repository, on offline storage, is better still.
PUBLIC: printed (and with --public-out written) as a C array; safe to commit.

Run it once per key, on a machine you trust; back the private key up offline.
"""
import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))
sys.path.insert(0, str(ROOT / "tools"))
from firmware_updates import valid_key_id  # noqa: E402
import release_signing  # noqa: E402


def unsafe_location(path):
    """True if path is inside this repository but not under private/."""
    resolved = path.resolve()
    try:
        resolved.relative_to(ROOT)
    except ValueError:
        return False
    try:
        resolved.relative_to(ROOT / "private")
        return False
    except ValueError:
        return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--key-id", required=True, help="e.g. homedeck-release-2026")
    parser.add_argument("--out", required=True, type=Path, help="private key file to create (PEM)")
    parser.add_argument("--public-out", type=Path, help="also write the C public-key snippet here")
    parser.add_argument("--no-encrypt", action="store_true",
                        help="store the private key without a passphrase (not recommended)")
    args = parser.parse_args(argv)

    if not valid_key_id(args.key_id):
        parser.error(f"--key-id {args.key_id!r}: 1-32 characters, a-z, 0-9 and '-'")
    if unsafe_location(args.out):
        parser.error(f"{args.out} is inside the repository; keep private keys outside it "
                     "(or, at most, under the git-ignored private/)")
    try:
        release_signing.generate_private_key(args.out, encrypt=not args.no_encrypt)
        public_key = release_signing.public_key_from_private(args.out)
    except release_signing.SigningError as error:
        parser.error(str(error))

    name = "RELEASE_KEY_" + args.key_id.upper().replace("-", "_")
    snippet = (f"// {args.key_id}: ECDSA P-256 public key (04 || X || Y). Public; safe to commit.\n"
               + release_signing.c_array(name, public_key) + "\n"
               + f'// Trusted slot entry: {{"{args.key_id}", {name}}}\n')
    if args.public_out:
        args.public_out.write_text(snippet)
    print(f"Private key written to {args.out} (mode 0600{'' if args.no_encrypt else ', passphrase-encrypted'}).")
    print("It was not printed. Back it up offline; never commit it or put it in .env.\n")
    print(f"Public key fingerprint (SHA-256): {hashlib.sha256(public_key).hexdigest()}\n")
    print(snippet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
